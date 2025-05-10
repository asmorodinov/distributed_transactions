LIBRARY()

SRCS(
    hybrid_logical_clock.cpp
)

PEERDIR(
    library/cpp/yt/assert
    library/cpp/yt/threading

    contrib/libs/protobuf
)

END()
