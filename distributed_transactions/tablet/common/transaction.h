#pragma once

#include "mvcc_storage.h"

#include <library/cpp/yt/threading/spin_lock.h>
#include <library/cpp/yt/threading/public.h>

#include <atomic>

namespace NMiniYT {

enum class ETransactionState {
    Unknown,
    Registered,
    Preparing,
    Prepared,
    Aborted,
    Commiting,
    Commited
};

class TTransaction {
public:
    explicit TTransaction(TTransactionID id);

    void Abort();
    void Commit(TTimestamp timestamp);

    void AddWriteIntent(const TKey& key, const TMaybe<TValue>& value);
    void AddToReadSet(TRow* row);
    void AddToWriteSet(TRow* row);

    TVector<TKey> GetWriteIntentKeys() const;
    TMap<TKey, TMaybe<TValue>> GetWriteIntents() const;
    TVector<TRow*> GetReadSet() const;

public:
    const TTimestamp ReadTimestamp;
    const TTransactionID ID;

    std::atomic<TTimestamp> CommitTimestamp = 0;
    std::atomic<ETransactionState> State = ETransactionState::Unknown;

private:
    YT_DECLARE_SPIN_LOCK(NYT::NThreading::TSpinLock, Lock_);

    TVector<TRow*> ReadSet_;
    TVector<TRow*> WriteSet_;
    TMap<TKey, TMaybe<TValue>> WriteIntents_;
};

}  // namespace NMiniYT
