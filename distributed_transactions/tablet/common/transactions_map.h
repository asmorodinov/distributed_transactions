#pragma once

#include "transaction.h"

namespace NMiniYT {

class TTransactionsMap {
public:
    const TTransaction& At(TTransactionID id) const;
    TTransaction& CreateNewTransaction(TTransactionID id);
    TTransaction& GetOrCreateTransaction(TTransactionID id);
    TTransaction& Get(TTransactionID id);
    TTransaction* FindPtr(TTransactionID id);

private:
    YT_DECLARE_SPIN_LOCK(NYT::NThreading::TSpinLock, Lock_);
    THashMap<TTransactionID, TTransaction> Transactions_;
};

}  // namespace NMiniYT
