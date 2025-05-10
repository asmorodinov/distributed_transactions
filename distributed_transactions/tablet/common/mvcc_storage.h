#pragma once

#include "transaction_id.h"

#include <library/cpp/yt/assert/assert.h>

#include <library/cpp/yt/threading/spin_lock.h>
#include <library/cpp/yt/threading/public.h>

#include <util/system/types.h>
#include <util/generic/string.h>
#include <util/generic/maybe.h>
#include <util/generic/map.h>
#include <util/generic/vector.h>
#include <util/generic/hash_set.h>

namespace NMiniYT {

using TTimestamp = ui64;

using TKey = TString;
using TValue = TString;

class TLock {
public:
    TLock() = default;

    TTransactionID TryTakeSharedLock(TTransactionID id);
    bool TryTakeExclusiveLock(TTransactionID id);
    void Unlock(TTransactionID id);

    bool IsExclusivelyOwned(TTransactionID id);
    bool IsOwned(TTransactionID id);

private:
    YT_DECLARE_SPIN_LOCK(NYT::NThreading::TSpinLock, Lock_);

    THashSet<TTransactionID> Readers_;
    TTransactionID Writer_ = NullTransactionID;
};

class TRow {
public:
    TMaybe<TValue> Lookup(TTimestamp timestamp) const;

    void Write(TTimestamp timestamp, TMaybe<TValue> value);
    void Insert(TTimestamp timestamp, TValue value);
    void Delete(TTimestamp timestamp);

    void UpdateMaxReadTimestamp(TTimestamp timestamp);

    TTimestamp GetMaxReadTimestamp() const;
    TTimestamp GetMaxWriteTimestamp() const;

public:
    TLock Lock; // !! Lock must be acquired when calling methods (exclusive for writes, shared for reads)

private:
    TMap<TTimestamp, TMaybe<TValue>> History_;
    TTimestamp MaxReadTimestamp_ = 0;
    TTimestamp MaxWriteTimestamp_ = 0; // insert or delete
};

class TMVCCStorage {
public:
    TMVCCStorage() = default;

    TRow& GetRow(const TKey& key);

private:
    YT_DECLARE_SPIN_LOCK(NYT::NThreading::TSpinLock, Lock_);
    TMap<TKey, TRow> Rows_;
};

}; // namespace NMiniYT
