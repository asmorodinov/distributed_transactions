#include "transactions_map.h"

#include <util/system/guard.h>

namespace NMiniYT {

    const TTransaction& TTransactionsMap::At(TTransactionID id) const {
        auto guard = Guard(Lock_);

        return Transactions_.at(id);
    }

    TTransaction& TTransactionsMap::CreateNewTransaction(TTransactionID id) {
        auto guard = Guard(Lock_);

        Y_ENSURE(!Transactions_.contains(id));
        Transactions_.try_emplace(id, id);
        return Transactions_.at(id);
    }

    TTransaction& TTransactionsMap::GetOrCreateTransaction(TTransactionID id) {
        auto guard = Guard(Lock_);

        if (!Transactions_.contains(id)) {
            Transactions_.try_emplace(id, id);
        }
        return Transactions_.at(id);
    }

    TTransaction& TTransactionsMap::Get(TTransactionID id) {
        auto guard = Guard(Lock_);

        return Transactions_.at(id);
    }

    TTransaction* TTransactionsMap::FindPtr(TTransactionID id) {
        auto guard = Guard(Lock_);

        return Transactions_.FindPtr(id);
    }

}  // namespace NMiniYT
