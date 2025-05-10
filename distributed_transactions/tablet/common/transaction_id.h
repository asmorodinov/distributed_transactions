#pragma once

#include <distributed_transactions/tablet/proto/transaction_id.pb.h>

#include <functional>

namespace NMiniYT {

using TTransactionID = NTablet::NApi::TTransactionID;

static const TTransactionID NullTransactionID = TTransactionID();

bool IsEqual(const NMiniYT::TTransactionID& lhs, const NMiniYT::TTransactionID& rhs);

}  // namespace NMiniYT

template<>
struct THash<NMiniYT::TTransactionID> {
    size_t operator()(const NMiniYT::TTransactionID& id) const;
};

template<>
struct std::equal_to<NMiniYT::TTransactionID> {
    bool operator()(const NMiniYT::TTransactionID& lhs, const NMiniYT::TTransactionID& rhs);
};
