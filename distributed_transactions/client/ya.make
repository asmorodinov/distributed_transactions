LIBRARY()

SRCS(
    transaction_client.cpp
    transaction_examples.cpp
)

PEERDIR(
    yt/yt/core
    library/cpp/getopt/small

    distributed_transactions/tablet/proto
    distributed_transactions/tablet/proxy
    distributed_transactions/tablet/common
)

END()
