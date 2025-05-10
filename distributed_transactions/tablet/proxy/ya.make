LIBRARY()

SRCS(
    rpc_proxy.cpp
)

PEERDIR(
    yt/yt/core

    distributed_transactions/tablet/proto
)

END()
