#include "mvcc_storage.h"

namespace NMiniYT {

    TTransactionID TLock::TryTakeSharedLock(TTransactionID id) {
        auto guard = Guard(Lock_);

        if (IsEqual(Writer_, NullTransactionID)) {
            Readers_.insert(id);
        }
        return Writer_;
    }

    bool TLock::TryTakeExclusiveLock(TTransactionID id) {
        auto guard = Guard(Lock_);

        if (IsEqual(Writer_, NullTransactionID) && (Readers_.empty() || (Readers_.size() == 1 && Readers_.contains(id)))) {
            Writer_ = id;
            Readers_.erase(id);
            return true;
        } else {
            return false;
        }
    }

    void TLock::Unlock(TTransactionID id) {
        auto guard = Guard(Lock_);

        if (IsEqual(Writer_, id)) {
            Writer_ = NullTransactionID;
        } else {
            Readers_.erase(id);
        }
    }

    bool TLock::IsExclusivelyOwned(TTransactionID id) {
        auto guard = Guard(Lock_);

        return IsEqual(Writer_, id);
    }

    bool TLock::IsOwned(TTransactionID id) {
        auto guard = Guard(Lock_);

        return IsEqual(Writer_, id) || Readers_.contains(id);
    }

    TMaybe<TValue> TRow::Lookup(TTimestamp timestamp) const {
        const auto it = History_.upper_bound(timestamp);
        if (it == History_.begin()) {
            return Nothing();
        }
        return std::prev(it)->second;
    }

    void TRow::Write(TTimestamp timestamp, TMaybe<TValue> value) {
        // sanity check: forbid commits to the past
        auto maxWriteTimestamp = MaxWriteTimestamp_.load();
        Y_ENSURE(timestamp > maxWriteTimestamp);

        History_[timestamp] = std::move(value);

        auto old = MaxWriteTimestamp_.exchange(timestamp);
        Y_ENSURE(old == maxWriteTimestamp);
    }

    void TRow::Insert(TTimestamp timestamp, TValue value) {
        Write(timestamp, std::move(value));
    }

    void TRow::Delete(TTimestamp timestamp) {
        Write(timestamp, Nothing());
    }

    void TRow::UpdateMaxReadTimestamp(TTimestamp timestamp) {
        MaxReadTimestamp_ = Max(MaxReadTimestamp_, timestamp);
    }

    TTimestamp TRow::GetMaxReadTimestamp() const {
        return MaxReadTimestamp_;
    }

    TTimestamp TRow::GetMaxWriteTimestamp() const {
        return MaxWriteTimestamp_.load();
    }

    TRow& TMVCCStorage::GetRow(const TKey& key) {
        auto guard = Guard(Lock_);

        return Rows_[key];
    }

}  // namespace NMiniYT
