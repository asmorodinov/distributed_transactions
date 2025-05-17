#include "transaction_client.h"

#include <util/random/random.h>

namespace NMiniYT {

    TTransactionClient::TTransactionClient(TVector<TAddress> tabletAddressess)
        : TabletAddressess_(std::move(tabletAddressess))
    {
        TabletProxies_.reserve(TabletAddressess_.size());
        for (const auto& address : TabletAddressess_) {
            AddTabletProxy(address);
        }
    }

    TVector<TAddress> TTransactionClient::GetParticipants(const TVector<TKey>& keys) const {
        auto participantsSet = THashSet<TAddress>();
        for (const auto& key : keys) {
            participantsSet.insert(GetTabletAddressForKey(key));
        }

        return {participantsSet.begin(), participantsSet.end()};
    }

    const TAddress& TTransactionClient::ChooseCoordinator(const TVector<TKey>& keys) const {
        const auto& key = keys[RandomNumber<size_t>(keys.size())];
        return GetTabletAddressForKey(key);
    }

    const TAddress& TTransactionClient::GetTabletAddressForKey(const TKey& key) const {
        const auto hash = THash<TKey>()(key);
        return TabletAddressess_[hash % TabletAddressess_.size()];
    }

    TTransactionID TTransactionClient::StartTransaction(const TAddress& coordinator) const {
        auto req = TabletProxies_.at(coordinator).StartTransaction();
        auto rsp = NYT::NConcurrency::WaitFor(req->Invoke()).ValueOrThrow();
        return rsp->GetTransactionID();
    }

    void TTransactionClient::AbortTransaction(const TTransactionID& transactionID, const TVector<TAddress>& participants) const {
        auto abortRequest = NTablet::NApi::TReqAbortTransaction();
        abortRequest.MutableTransactionID()->CopyFrom(transactionID);

        auto abortFutures = TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspAbortTransaction>::TResult>>();
        abortFutures.reserve(participants.size());

        for (const auto& address : participants) {
            auto req = TabletProxies_.at(address).AbortTransaction();
            req->CopyFrom(abortRequest);
            abortFutures.push_back(req->Invoke());
        }

        NYT::NConcurrency::WaitFor(NYT::AllSet(abortFutures)).ValueOrThrow();
    }

    TMaybe<TVector<TValue>> TTransactionClient::ReadRows(const TTransactionID& transactionID, const TVector<TKey>& keys) const {
        auto readFutures = TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspReadRow>::TResult>>();
        readFutures.reserve(keys.size());

        for (const auto& key : keys) {
            auto req = TabletProxies_.at(GetTabletAddressForKey(key)).ReadRow();
            req->SetKey(key);
            req->MutableTransactionID()->CopyFrom(transactionID);
            readFutures.push_back(req->Invoke());
        }

        auto readResults = NYT::NConcurrency::WaitFor(NYT::AllSet(readFutures)).ValueOrThrow();

        for (const auto& result : readResults) {
            if (result.ValueOrThrow()->HasError()) {
                return Nothing(); // transaction failed
            }
        }

        auto values = TVector<TValue>(); // TValue instead of TMaybe<TValue> for simplicity sake
        values.reserve(keys.size());

        for (const auto& result : readResults) {
            values.push_back(std::move(result.Value()->GetValue()));
        }

        return values;
    }

    TTimestamp TTransactionClient::GetMinimumMaxWriteTimestamp(const TVector<TKey>& keys) const {
        auto futures = TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspGetMaxWriteTimestamp>::TResult>>();
        futures.reserve(keys.size());

        for (const auto& key : keys) {
            auto req = TabletProxies_.at(GetTabletAddressForKey(key)).GetMaxWriteTimestamp();
            req->SetKey(key);
            futures.push_back(req->Invoke());
        }

        auto results = NYT::NConcurrency::WaitFor(NYT::AllSet(futures)).ValueOrThrow();

        auto timestamp = Max<TTimestamp>();
        for (const auto& result : results) {
            timestamp = Min(timestamp, result.ValueOrThrow()->GetTimestamp());
        }

        return timestamp;
    }

    TVector<TValue> TTransactionClient::ReadAtTimestamp(TTimestamp timestamp, const TVector<TKey>& keys) const {
        auto readFutures = TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspReadAtTimestamp>::TResult>>();
        readFutures.reserve(keys.size());

        for (const auto& key : keys) {
            auto req = TabletProxies_.at(GetTabletAddressForKey(key)).ReadAtTimestamp();
            req->SetKey(key);
            req->SetTimestamp(timestamp);
            readFutures.push_back(req->Invoke());
        }

        auto readResults = NYT::NConcurrency::WaitFor(NYT::AllSet(readFutures)).ValueOrThrow();

        for (const auto& result : readResults) {
            if (result.ValueOrThrow()->HasError()) {
                throw yexception() << "failed to read at timestamp, got error: " << result.ValueOrThrow()->GetError();
            }
        }

        auto values = TVector<TValue>(); // TValue instead of TMaybe<TValue> for simplicity sake
        values.reserve(keys.size());

        for (const auto& result : readResults) {
            values.push_back(std::move(result.Value()->GetValue()));
        }

        return values;
    }

    void TTransactionClient::SendWriteIntents(const TTransactionID& transactionID, const TVector<TItem>& items) const {
        auto writeIntentFutures = TVector<NYT::TFuture<typename NYT::NRpc::TTypedClientResponse<NTablet::NApi::TRspWriteIntent>::TResult>>();
        writeIntentFutures.reserve(items.size());

        for (const auto& [key, value] : items) {
            auto req = TabletProxies_.at(GetTabletAddressForKey(key)).WriteIntent();
            req->SetKey(key);
            req->SetValue(value);
            req->MutableTransactionID()->CopyFrom(transactionID);
            writeIntentFutures.push_back(req->Invoke());
        }

        NYT::NConcurrency::WaitFor(NYT::AllSet(writeIntentFutures)).ValueOrThrow();
    }

    bool TTransactionClient::Commit(const TTransactionID& transactionID, const TAddress& coordinator, const TVector<TAddress>& participants) const {
        auto req = TabletProxies_.at(coordinator).ClientCommitTransaction();
        req->MutableTransactionID()->CopyFrom(transactionID);
        for (const auto& participant : participants) {
            if (participant != coordinator) {
                req->AddOtherParticipantAddresses(participant);
            }
        }

        auto rsp = NYT::NConcurrency::WaitFor(req->Invoke()).ValueOrThrow();
        return !rsp->HasError();
    }

    void TTransactionClient::AddTabletProxy(TAddress address) {
        auto client = NYT::NBus::CreateBusClient(NYT::NBus::TBusClientConfig::CreateTcp(address));
        auto channel = NYT::NRpc::NBus::CreateBusChannel(client);
        auto proxy = TCoordinatorProxy(channel);
        proxy.SetDefaultAcknowledgementTimeout(std::nullopt);

        TabletProxies_.insert({address, std::move(proxy)});
    }

} // namespace NMiniYT
