#include "coordinator.h"
#include "participant.h"

#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/bus/tcp/client.h>
#include <yt/yt/core/rpc/bus/channel.h>

#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/actions/future.h>

namespace NMiniYT {

    ICoordinatorService::ICoordinatorService(ui64 id, NYT::IInvokerPtr invoker, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions)
        : TServiceBase(
            std::move(invoker),
            TCoordinatorProxy::GetDescriptor(),
            CoordinatorLogger())
        , ID_(id)
        , Storage_(storage)
        , Transactions_(transactions)
    {
        for (const auto& address : otherParticipantAddresses) {
            AddParticipantProxy(address);
        }

        RegisterMethod(RPC_SERVICE_METHOD_DESC(StartTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ReadRow));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetMaxWriteTimestamp));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ReadAtTimestamp));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(WriteIntent));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ClientCommitTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortTransaction));
    }

    void ICoordinatorService::OnReceiveMessage(const google::protobuf::Message&) {}
    void ICoordinatorService::OnSendMessage(google::protobuf::Message&) {}

    void ICoordinatorService::AddParticipantProxy(const TString& address) {
        auto client = NYT::NBus::CreateBusClient(NYT::NBus::TBusClientConfig::CreateTcp(address));
        auto channel = NYT::NRpc::NBus::CreateBusChannel(client);
        auto proxy = TParticipantProxy(channel);
        proxy.SetDefaultAcknowledgementTimeout(std::nullopt);

        TabletProxies_.insert({address, std::move(proxy)});
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, StartTransaction)
    {
        OnReceiveMessage(*request);

        TTimestamp timestamp = GetTimestamp();

        TTransactionID id;
        id.SetTimestamp(timestamp);
        id.SetCoordinatorID(ID_);

        Transactions_.CreateNewTransaction(id);

        response->MutableTransactionID()->CopyFrom(id);

        OnSendMessage(*response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, ReadRow)
    {
        OnReceiveMessage(*request);

        const auto& transactionID = request->GetTransactionID();

        auto& row = Storage_.GetRow(request->GetKey());

        // transaction could have been created via another tablet => current tablet might not know about it
        auto& transaction = Transactions_.GetOrCreateTransaction(transactionID);

        auto state = transaction.State.load();
        if (state == ETransactionState::Aborted) {
            response->SetError("can not read row within aborted transaction");
            OnSendMessage(*response);
            context->Reply();
            return;
        }

        if (state != ETransactionState::Registered) {
            YT_LOG_FATAL("unexpected transaction state: %v", static_cast<int>(state));
        }

        while (true) {
            const auto writerID = row.Lock.TryTakeSharedLock(transactionID);
            if (IsEqual(writerID, NullTransactionID)) {
                // successfully acquired the lock
                transaction.AddToReadSet(&row);

                if (row.GetMaxWriteTimestamp() > transaction.ReadTimestamp) {
                    // rw conflict with concurrently running transaction
                    transaction.Abort();
                    response->SetError("transaction aborted due to concurrently running transaction (rw conflict)");

                    OnSendMessage(*response);
                    context->Reply();
                    return;
                }

                const auto value = row.Lookup(transaction.ReadTimestamp);
                if (value.Defined()) {
                    response->SetValue(value.GetRef());
                }

                OnSendMessage(*response);
                context->Reply();
                return;
            } else {
                // read is blocked
                if (transaction.ReadTimestamp < writerID.GetTimestamp()) {
                    // wait-die: abort current transaction (non-preemptive deadlock prevention)
                    transaction.Abort();
                    response->SetError("transaction aborted to prevent deadlocks");

                    OnSendMessage(*response);
                    context->Reply();
                    return;
                } else {
                    // wait-die: wait for other transaction to release the lock
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MicroSeconds(10));
                }
            }
        }
        YT_UNREACHABLE();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, GetMaxWriteTimestamp)
    {
        OnReceiveMessage(*request);

        auto& row = Storage_.GetRow(request->GetKey());

        const auto maxWriteTimestamp = row.GetMaxWriteTimestamp();
        response->SetTimestamp(maxWriteTimestamp);

        OnSendMessage(*response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, ReadAtTimestamp)
    {
        OnReceiveMessage(*request);

        auto& row = Storage_.GetRow(request->GetKey());

        const auto maxWriteTimestamp = row.GetMaxWriteTimestamp();

        if (request->GetTimestamp() > maxWriteTimestamp) {
            response->SetError("timestamp > last commited timestamp => read can be non-atomic");
            OnSendMessage(*response);
            context->Reply();
            return;
        }

        const auto value = row.Lookup(request->GetTimestamp());
        if (value.Defined()) {
            response->SetValue(value.GetRef());
        }

        OnSendMessage(*response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, WriteIntent)
    {
        OnReceiveMessage(*request);

        const auto& transactionID = request->GetTransactionID();

        // transaction could have been created via another tablet => current tablet might not know about it
        auto& transaction = Transactions_.GetOrCreateTransaction(transactionID);
        YT_VERIFY(transaction.State.load() == ETransactionState::Registered);

        const auto value = request->HasValue() ? TMaybe<TValue>(request->GetValue()) : Nothing();
        transaction.AddWriteIntent(request->GetKey(), value);

        OnSendMessage(*response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, ClientCommitTransaction)
    {
        OnReceiveMessage(*request);

        const auto& transactionID = request->GetTransactionID();

        // it does not make sense to commit transaction, if there were no reads or write intents
        auto& transaction = Transactions_.Get(transactionID);
        YT_VERIFY(transaction.State.load() == ETransactionState::Registered);

        const auto& otherParticipants = request->GetOtherParticipantAddresses();

        // 2PC: first phase

        auto thisParticipant = TParticipantServiceBase(Storage_, Transactions_);

        NTablet::NApi::TReqParticipantPrepareTransaction prepareRequest;
        NTablet::NApi::TRspParticipantPrepareTransaction localPrepareResponse;
        prepareRequest.MutableTransactionID()->CopyFrom(transactionID);
        OnSendMessage(prepareRequest);

        thisParticipant.DoParticipantPrepareTransaction(prepareRequest, localPrepareResponse);

        bool prepareSuccess = true;
        if (!localPrepareResponse.GetIsSuccess()) {
            prepareSuccess = false;
        }

        if (prepareSuccess) {
            TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspParticipantPrepareTransaction>::TResult>> prepareFutures;
            prepareFutures.reserve(otherParticipants.size());

            for (const auto& address : otherParticipants) {
                auto req = TabletProxies_.at(address).ParticipantPrepareTransaction();
                req->CopyFrom(prepareRequest);
                prepareFutures.push_back(req->Invoke());
            }
            auto prepareResults = NYT::NConcurrency::WaitFor(NYT::AllSet(prepareFutures)).ValueOrThrow();

            for (const auto& result : prepareResults) {
                if (result.IsOK()) {
                    OnReceiveMessage(*result.Value());
                }

                if (!result.IsOK() || !result.Value()->GetIsSuccess()) {
                    prepareSuccess = false;
                    break;
                }
            }
        }

        if (!prepareSuccess) {
            // abort
            transaction.Abort();

            NTablet::NApi::TReqParticipantAbortTransaction abortRequest;
            abortRequest.MutableTransactionID()->CopyFrom(transactionID);
            OnSendMessage(abortRequest);

            TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspParticipantAbortTransaction>::TResult>> abortFutures;
            abortFutures.reserve(otherParticipants.size());
            for (const auto& address : otherParticipants) {
                auto req = TabletProxies_.at(address).ParticipantAbortTransaction();
                req->CopyFrom(abortRequest);
                abortFutures.push_back(req->Invoke());
            }
            NYT::NConcurrency::WaitFor(NYT::AllSet(abortFutures)).ValueOrThrow();

            response->SetError("Prepare phase failed: transaction was aborted");
            context->Reply();
            return;
        }

        // 2PC: second phase
        TTimestamp commitTimestamp = GetTimestamp();

        NTablet::NApi::TReqParticipantCommitTransaction commitRequest;
        NTablet::NApi::TRspParticipantCommitTransaction localCommitResponse;
        commitRequest.MutableTransactionID()->CopyFrom(transactionID);
        commitRequest.SetCommitTimestamp(commitTimestamp);
        OnSendMessage(commitRequest);
        thisParticipant.DoParticipantCommitTransaction(commitRequest, localCommitResponse);

        TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspParticipantCommitTransaction>::TResult>> commitFutures;
        commitFutures.reserve(otherParticipants.size());
        for (const auto& address : otherParticipants) {
            auto req = TabletProxies_.at(address).ParticipantCommitTransaction();
            req->CopyFrom(commitRequest);
            commitFutures.push_back(req->Invoke());
        }
        auto commitResults = NYT::NConcurrency::WaitFor(NYT::AllSet(commitFutures)).ValueOrThrow();

        for (const auto& result : commitResults) {
            OnReceiveMessage(*result.ValueOrThrow());
        }

        response->SetCommitTimestamp(commitTimestamp);

        OnSendMessage(*response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(ICoordinatorService, AbortTransaction)
    {
        OnReceiveMessage(*request);

        const auto& transactionID = request->GetTransactionID();
        if (auto transaction = Transactions_.FindPtr(transactionID); transaction != nullptr) {
            transaction->Abort();
        }

        OnSendMessage(*response);
        context->Reply();
    }

    TCoordinatorService::TCoordinatorService(ui64 id, NYT::IInvokerPtr invoker, const TString& timestampProviderAddress, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions)
        : ICoordinatorService(id, std::move(invoker), otherParticipantAddresses, storage, transactions)
        , TimestampProviderProxy_(CreateTimestampProvider(timestampProviderAddress))
    {
    }

    TTimestamp TCoordinatorService::GetTimestamp() {
        auto req = TimestampProviderProxy_.GenerateTimestamp();
        auto rsp = NYT::NConcurrency::WaitFor(req->Invoke()).ValueOrThrow();
        return rsp->GetTimestamp();
    }

    TTimestampProviderProxy TCoordinatorService::CreateTimestampProvider(const TString& address) {
        auto client = NYT::NBus::CreateBusClient(NYT::NBus::TBusClientConfig::CreateTcp(address));
        auto channel = NYT::NRpc::NBus::CreateBusChannel(client);
        auto proxy = TTimestampProviderProxy(channel);
        proxy.SetDefaultAcknowledgementTimeout(std::nullopt);

        return proxy;
    }

    TCoordinatorServiceWithHLC::TCoordinatorServiceWithHLC(ui64 id, NYT::IInvokerPtr invoker, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock& clock)
        : ICoordinatorService(id, std::move(invoker), otherParticipantAddresses, storage, transactions)
        , Clock_(clock)
    {
    }

    TTimestamp TCoordinatorServiceWithHLC::GetTimestamp() {
        return Clock_.GetTimestamp();
    }

    void TCoordinatorServiceWithHLC::OnReceiveMessage(const google::protobuf::Message& message) {
        Clock_.OnReceiveMessage(message);
    }

    void TCoordinatorServiceWithHLC::OnSendMessage(google::protobuf::Message& message) {
        Clock_.OnSendMessage(message);
    }

}  // namespace NMiniYT
