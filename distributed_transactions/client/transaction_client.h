#pragma once

#include <distributed_transactions/tablet/proxy/rpc_proxy.h>
#include <distributed_transactions/tablet/common/transaction_id.h>
#include <distributed_transactions/tablet/common/mvcc_storage.h>

#include <yt/yt/core/bus/tcp/client.h>
#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/rpc/bus/channel.h>

namespace NMiniYT {

struct TItem {
    TKey Key;
    TValue Value; // no TMaybe for simplicity sake
};

using TAddress = TString;

class TTransactionClient {
public:
    explicit TTransactionClient(TVector<TAddress> tabletAddressess);

    TVector<TAddress> GetParticipants(const TVector<TKey>& keys) const;

    const TAddress& ChooseCoordinator(const TVector<TKey>& keys) const;

    const TAddress& GetTabletAddressForKey(const TKey& key) const;

    TTransactionID StartTransaction(const TAddress& coordinator) const;

    void AbortTransaction(const TTransactionID& transactionID, const TVector<TAddress>& participants) const;

    TMaybe<TVector<TValue>> ReadRows(const TTransactionID& transactionID, const TVector<TKey>& keys) const;

    TTimestamp GetMinimumMaxWriteTimestamp(const TVector<TKey>& keys) const;
    TVector<TValue> ReadAtTimestamp(const TTimestamp timestamp, const TVector<TKey>& keys) const;

    void SendWriteIntents(const TTransactionID& transactionID, const TVector<TItem>& items) const;

    bool Commit(const TTransactionID& transactionID, const TAddress& coordinator, const TVector<TAddress>& participants) const;

private:
    void AddTabletProxy(TAddress address);

private:
    TVector<TAddress> TabletAddressess_;
    THashMap<TAddress, TCoordinatorProxy> TabletProxies_;
};

} // namespace NMiniYT
