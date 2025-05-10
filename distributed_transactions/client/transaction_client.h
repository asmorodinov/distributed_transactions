#pragma once

#include <distributed_transactions/tablet/proto/coordinator_api.pb.h>
#include <distributed_transactions/tablet/proxy/rpc_proxy.h>
#include <distributed_transactions/tablet/common/transaction_id.h>
#include <distributed_transactions/tablet/common/mvcc_storage.h>

#include <yt/yt/core/bus/tcp/client.h>
#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/rpc/bus/channel.h>
#include <yt/yt/core/profiling/timing.h>

#include <library/cpp/getopt/last_getopt.h>
#include <library/cpp/iterator/zip.h>

#include <util/random/random.h>

namespace NMiniYT {

struct TItem {
    TKey Key;
    TValue Value; // no TMaybe for simplicity sake
};

using TAddress = TString;

using TTimestamp = ui64;

class TTransactionClient {
public:
    TTransactionClient(TVector<TAddress> tabletAddressess)
        : TabletAddressess_(std::move(tabletAddressess))
    {
        TabletProxies_.reserve(TabletAddressess_.size());
        for (const auto& address : TabletAddressess_) {
            AddTabletProxy(address);
        }
    }

    TVector<TAddress> GetParticipants(const TVector<TKey>& keys) const {
        auto participantsSet = THashSet<TAddress>();
        for (const auto& key : keys) {
            participantsSet.insert(GetTabletAddressForKey(key));
        }

        return {participantsSet.begin(), participantsSet.end()};
    }

    const TAddress& ChooseCoordinator(const TVector<TKey>& keys) const {
        const auto& key = keys[RandomNumber<size_t>(keys.size())];
        return GetTabletAddressForKey(key);
    }

    const TAddress& GetTabletAddressForKey(const TKey& key) const {
        const auto hash = THash<TKey>()(key);
        return TabletAddressess_[hash % TabletAddressess_.size()];
    }

    TTransactionID StartTransaction(const TAddress& coordinator) const {
        auto req = TabletProxies_.at(coordinator).StartTransaction();
        auto rsp = NYT::NConcurrency::WaitFor(req->Invoke()).ValueOrThrow();
        return rsp->GetTransactionID();
    }

    void AbortTransaction(const TTransactionID& transactionID, const TVector<TAddress>& participants) const {
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

    TMaybe<TVector<TValue>> ReadRows(const TTransactionID& transactionID, const TVector<TKey>& keys) const {
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

    void SendWriteIntents(const TTransactionID& transactionID, const TVector<TItem>& items) const {
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

    bool Commit(const TTransactionID& transactionID, const TAddress& coordinator, const TVector<TAddress>& participants) const {
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

private:
    void AddTabletProxy(TAddress address) {
        auto client = NYT::NBus::CreateBusClient(NYT::NBus::TBusClientConfig::CreateTcp(address));
        auto channel = NYT::NRpc::NBus::CreateBusChannel(client);
        auto proxy = TCoordinatorProxy(channel);
        proxy.SetDefaultAcknowledgementTimeout(std::nullopt);

        TabletProxies_.insert({address, std::move(proxy)});
    }

private:
    TVector<TAddress> TabletAddressess_;
    THashMap<TAddress, TCoordinatorProxy> TabletProxies_;
};

} // namespace NMiniYT
