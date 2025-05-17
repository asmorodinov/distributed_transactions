LIBRARY()

SRCS(
    transaction_client.cpp
    transaction_examples.cpp
)

PEERDIR(
    yt/yt/core

    distributed_transactions/tablet/proxy
    distributed_transactions/tablet/common
)

END()
