PROGRAM()

SRCS(
    main.cpp
)

PEERDIR(
    yt/yt/core
    library/cpp/getopt/small

    distributed_transactions/timestamp_provider/proto
    distributed_transactions/timestamp_provider/proxy
    distributed_transactions/common
)

END()
