PROGRAM()

SRCS(
    coordinator.cpp
    participant.cpp
    main.cpp
)

PEERDIR(
    yt/yt/core
    library/cpp/getopt/small

    distributed_transactions/tablet/common
    distributed_transactions/tablet/hlc
    distributed_transactions/tablet/proto
    distributed_transactions/tablet/proxy

    distributed_transactions/timestamp_provider/proxy

    distributed_transactions/common
)

END()
