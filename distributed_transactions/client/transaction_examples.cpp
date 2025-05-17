#include "transaction_examples.h"

namespace NMiniYT {

i64 AsInt(const TValue& value) {
    if (value.empty()) {
        return 0;
    }
    return FromString<i64>(value);
}

TValue AsString(i64 value) {
    return ToString(value);
}

ETransactionResult InsertTransaction(const TTransactionClient& client, const TVector<TKey>& keys, const TVector<TKey>& values) {
    const auto& coordinator = client.ChooseCoordinator(keys);
    const auto& participants = client.GetParticipants(keys);

    const auto transactionID = client.StartTransaction(coordinator);

    auto items = TVector<TItem>();
    items.reserve(keys.size());
    for (const auto& [key, value] : Zip(keys, values)) {
        items.emplace_back(key, value);
    }

    client.SendWriteIntents(transactionID, items);

    if (client.Commit(transactionID, coordinator, participants)) {
        return ETransactionResult::OK;
    } else {
        return ETransactionResult::CommitFailed;
    }
}

ETransactionResult AddTransaction(const TTransactionClient& client, const TVector<TKey>& keys, const TVector<i64>& deltas) {
    return ReadWriteTransaction(client, keys, [&](const TVector<TValue>& values) {
        auto result = TVector<TItem>();
        result.reserve(keys.size());

        for (const auto& [key, value, delta] : Zip(keys, values, deltas)) {
            result.emplace_back(key, AsString(AsInt(value) + delta));
        }

        return result;
    });
}

ETransactionResult TransferTransaction(const TTransactionClient& client, const TVector<TKey>& allKeys, const TVector<i64>& deltas) {
    const auto n = deltas.size();
    if (allKeys.size() != 2 * n) {
        throw std::runtime_error("wrong number of keys");
    }

    return ReadWriteTransaction(client, allKeys, [&](const TVector<TValue>& allValues) {
        auto result = TVector<TItem>();
        result.reserve(allValues.size());

        for (size_t i = 0; i < n; ++i) {
            result.emplace_back(allKeys[i], AsString(AsInt(allValues[i]) - deltas[i]));
            result.emplace_back(allKeys[i + n], AsString(AsInt(allValues[i + n]) + deltas[i]));
        }

        return result;
    });
}

} // namespace NMiniYT
