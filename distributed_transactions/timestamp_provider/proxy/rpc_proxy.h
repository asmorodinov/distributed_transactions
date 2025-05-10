#pragma once

#include <distributed_transactions/timestamp_provider/proto/api.pb.h>

#include <yt/yt/core/rpc/client.h>

namespace NMiniYT {

class TTimestampProviderProxy
    : public NYT::NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TTimestampProviderProxy, GenerateTimestamp);

    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTimestampProvider::NApi, GenerateTimestamp);
};

}; // namespace NMiniYT
