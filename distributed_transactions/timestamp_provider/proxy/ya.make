LIBRARY()

SRCS(
    rpc_proxy.cpp
)

PEERDIR(
    yt/yt/core

    distributed_transactions/timestamp_provider/proto
)

END()
