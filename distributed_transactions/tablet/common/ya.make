LIBRARY()

SRCS(
    mvcc_storage.cpp
    transaction_id.cpp
    transaction.cpp
    transactions_map.cpp
)

PEERDIR(
    library/cpp/yt/assert
    library/cpp/yt/threading

    distributed_transactions/tablet/proto
)

END()
