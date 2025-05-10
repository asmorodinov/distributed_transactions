#include "transaction_id.h"

namespace NMiniYT {

bool IsEqual(const TTransactionID& lhs, const TTransactionID& rhs) {
    return lhs.GetCoordinatorID() == rhs.GetCoordinatorID() && lhs.GetTimestamp() == rhs.GetTimestamp();
}

}  // namespace NMiniYT

size_t THash<NMiniYT::TTransactionID>::operator()(const NMiniYT::TTransactionID& id) const {
    const auto h1 = std::hash<ui64>{}(id.GetCoordinatorID());
    const auto h2 = std::hash<ui64>{}(id.GetTimestamp());
    return h1 ^ (h2 << 1);
}

bool std::equal_to<NMiniYT::TTransactionID>::operator()(const NMiniYT::TTransactionID& lhs, const NMiniYT::TTransactionID& rhs) {
    return NMiniYT::IsEqual(lhs, rhs);
}
