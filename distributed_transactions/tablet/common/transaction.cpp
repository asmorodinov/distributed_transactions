#include "transaction.h"

namespace NMiniYT {

    TTransaction::TTransaction(TTransactionID id)
        : ReadTimestamp(id.GetTimestamp())
        , ID(id)
        , State(ETransactionState::Registered)
    {
    }

    void TTransaction::Abort() {
        auto guard = Guard(Lock_);

        State = ETransactionState::Aborted;

        for (auto row : ReadSet_) {
            row->Lock.Unlock(ID);
        }
        for (auto row : WriteSet_) {
            row->Lock.Unlock(ID);
        }
        ReadSet_.clear();
        WriteSet_.clear();
        WriteIntents_.clear();
    }

    void TTransaction::Commit(TTimestamp timestamp) {
        auto guard = Guard(Lock_);

        YT_ASSERT(State == ETransactionState::Commiting);
        State = ETransactionState::Commited;

        CommitTimestamp = timestamp;
        ReadSet_.clear();
        WriteSet_.clear();
        WriteIntents_.clear();
    }

    void TTransaction::AddWriteIntent(const TKey& key, const TMaybe<TValue>& value) {
        auto guard = Guard(Lock_);

        WriteIntents_[key] = value;
    }

    void TTransaction::AddToReadSet(TRow* row) {
        auto guard = Guard(Lock_);

        ReadSet_.push_back(row);
    }

    void TTransaction::AddToWriteSet(TRow* row) {
        auto guard = Guard(Lock_);

        WriteSet_.push_back(row);
    }

    TVector<TKey> TTransaction::GetWriteIntentKeys() const {
        auto guard = Guard(Lock_);

        auto result = TVector<TKey>();
        result.reserve(WriteIntents_.size());

        for (const auto& [key, value] : WriteIntents_) {
            result.push_back(key);
        }

        return result;
    }

    TMap<TKey, TMaybe<TValue>> TTransaction::GetWriteIntents() const {
        auto guard = Guard(Lock_);

        return WriteIntents_;
    }

    TVector<TRow*> TTransaction::GetReadSet() const {
        auto guard = Guard(Lock_);

        return ReadSet_;
    }

}  // namespace NMiniYT
