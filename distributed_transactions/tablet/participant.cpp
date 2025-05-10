#include "participant.h"

namespace NMiniYT {

    TParticipantServiceBase::TParticipantServiceBase(TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock* clock)
        : Storage_(storage)
        , Transactions_(transactions)
        , Clock_(clock)
    {
    }

    void TParticipantServiceBase::OnReceiveMessage(const google::protobuf::Message& message) {
        if (Clock_ != nullptr) {
            Clock_->OnReceiveMessage(message);
        }
    }

    void TParticipantServiceBase::OnSendMessage(google::protobuf::Message& message) {
        if (Clock_ != nullptr) {
            Clock_->OnSendMessage(message);
        }
    }

    void TParticipantServiceBase::DoParticipantPrepareTransaction(const NTablet::NApi::TReqParticipantPrepareTransaction& request, NTablet::NApi::TRspParticipantPrepareTransaction& response)
    {
        OnReceiveMessage(request);

        const auto& transactionID = request.GetTransactionID();

        // it does not make sense to prepare transaction, if there were no reads or write intents
        auto& transaction = Transactions_.Get(transactionID);

        const auto prevState = transaction.State.exchange(ETransactionState::Preparing);
        YT_ASSERT(prevState == ETransactionState::Registered);

        for (const auto& key : transaction.GetWriteIntentKeys()) {
            auto& row = Storage_.GetRow(key);
            if (row.Lock.TryTakeExclusiveLock(transactionID)) {
                // success (lock taken)
                transaction.AddToWriteSet(&row);

                if (row.GetMaxWriteTimestamp() > transaction.ReadTimestamp || row.GetMaxReadTimestamp() > transaction.ReadTimestamp) {
                    // rw or ww conflict with concurrently running transaction
                    transaction.Abort();
                    response.SetIsSuccess(false);
                    response.SetError("transaction aborted due to concurrently running transaction (rw or ww conflict)");
                    OnSendMessage(response);
                    return;
                }
            } else {
                // failed to take lock => abort
                transaction.Abort();
                response.SetIsSuccess(false);
                response.SetError("transaction aborted (failed to acquire write lock)");
                OnSendMessage(response);
                return;
            }
        }

        // all locks successfully acquired

        const auto state = transaction.State.exchange(ETransactionState::Prepared);
        YT_ASSERT(state == ETransactionState::Preparing);

        response.SetIsSuccess(true);
        OnSendMessage(response);
    }

    void TParticipantServiceBase::DoParticipantCommitTransaction(const NTablet::NApi::TReqParticipantCommitTransaction& request, NTablet::NApi::TRspParticipantCommitTransaction& response)
    {
        OnReceiveMessage(request);
        const auto& transactionID = request.GetTransactionID();
        const TTimestamp commitTimestamp = request.GetCommitTimestamp();

        // it does not make sense to commit transaction, if there was no prepare before
        auto& transaction = Transactions_.Get(transactionID);

        const auto prevState = transaction.State.exchange(ETransactionState::Commiting);
        YT_ASSERT(prevState == ETransactionState::Prepared);

        for (const auto& [key, value] : transaction.GetWriteIntents()) {
            auto& row = Storage_.GetRow(key);
            YT_ASSERT(row.Lock.IsExclusivelyOwned(transactionID));
            row.Write(commitTimestamp, value);
        }
        for (auto row : transaction.GetReadSet()) {
            YT_ASSERT(row->Lock.IsOwned(transactionID));
            row->UpdateMaxReadTimestamp(commitTimestamp);
        }

        for (const auto& [key, value] : transaction.GetWriteIntents()) {
            auto& row = Storage_.GetRow(key);
            row.Lock.Unlock(transactionID);
        }
        for (auto row : transaction.GetReadSet()) {
            row->Lock.Unlock(transactionID);
        }

        transaction.Commit(commitTimestamp);

        OnSendMessage(response);
    }

    void TParticipantServiceBase::DoParticipantAbortTransaction(const NTablet::NApi::TReqParticipantAbortTransaction& request)
    {
        const auto& transactionID = request.GetTransactionID();
        if (auto transaction = Transactions_.FindPtr(transactionID); transaction != nullptr) {
            transaction->Abort();
        }
    }

    TParticipantService::TParticipantService(NYT::IInvokerPtr invoker, TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock* clock)
        : TParticipantServiceBase(storage, transactions, clock)
        , TServiceBase(
            std::move(invoker),
            TParticipantProxy::GetDescriptor(),
            ParticipantLogger())
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ParticipantPrepareTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ParticipantCommitTransaction));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ParticipantAbortTransaction));
    }

    DEFINE_RPC_SERVICE_METHOD(TParticipantService, ParticipantPrepareTransaction)
    {
        DoParticipantPrepareTransaction(*request, *response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(TParticipantService, ParticipantCommitTransaction)
    {
        DoParticipantCommitTransaction(*request, *response);
        context->Reply();
    }

    DEFINE_RPC_SERVICE_METHOD(TParticipantService, ParticipantAbortTransaction)
    {
        DoParticipantAbortTransaction(*request);
        context->Reply();
    }

}  // namespace NMiniYT
