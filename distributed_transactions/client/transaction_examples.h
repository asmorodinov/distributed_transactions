#pragma once

#include "transaction_client.h"

#include <library/cpp/iterator/zip.h>

namespace NMiniYT {

i64 AsInt(const TValue& value);

TValue AsString(i64 value);

enum class ETransactionResult {
    OK = 0,
    ReadFailed = 1,
    CommitFailed = 2
};

ETransactionResult ReadWriteTransaction(const TTransactionClient& client, const TVector<TKey>& keys, auto&& func) {
    const auto& coordinator = client.ChooseCoordinator(keys);
    const auto& participants = client.GetParticipants(keys);

    const auto transactionID = client.StartTransaction(coordinator);
    const auto readValues = client.ReadRows(transactionID, keys);

    if (!readValues.Defined()) {
        client.AbortTransaction(transactionID, participants);
        return ETransactionResult::ReadFailed;
    }

    const auto items = func(readValues.GetRef());
    if (!items.empty()) {
        client.SendWriteIntents(transactionID, items);
    }

    if (client.Commit(transactionID, coordinator, participants)) {
        return ETransactionResult::OK;
    } else {
        return ETransactionResult::CommitFailed;
    }
}

ETransactionResult ReadOnlyTransaction(const TTransactionClient& client, const TVector<TKey>& keys, auto&& func) {
    const auto& coordinator = client.ChooseCoordinator(keys);
    const auto& participants = client.GetParticipants(keys);

    const auto transactionID = client.StartTransaction(coordinator);
    const auto readValues = client.ReadRows(transactionID, keys);

    if (!readValues.Defined()) {
        client.AbortTransaction(transactionID, participants);
        return ETransactionResult::ReadFailed;
    }

    if (client.Commit(transactionID, coordinator, participants)) {
        func(readValues.GetRef());
        return ETransactionResult::OK;
    } else {
        return ETransactionResult::CommitFailed;
    }
}

ETransactionResult InsertTransaction(const TTransactionClient& client, const TVector<TKey>& keys, const TVector<TKey>& values);
ETransactionResult AddTransaction(const TTransactionClient& client, const TVector<TKey>& keys, const TVector<i64>& deltas);
ETransactionResult TransferTransaction(const TTransactionClient& client, const TVector<TKey>& allKeys, const TVector<i64>& deltas);

} // namespace
