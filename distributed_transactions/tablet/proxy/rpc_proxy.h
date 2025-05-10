#pragma once

#include <distributed_transactions/tablet/proto/coordinator_api.pb.h>
#include <distributed_transactions/tablet/proto/participant_api.pb.h>

#include <yt/yt/core/rpc/client.h>

namespace NMiniYT {

class TCoordinatorProxy : public NYT::NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TCoordinatorProxy, Coordinator);

    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, StartTransaction);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, ReadRow);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, WriteIntent);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, ClientCommitTransaction);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, AbortTransaction);
};

class TParticipantProxy : public NYT::NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TParticipantProxy, Participant);

    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, ParticipantPrepareTransaction);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, ParticipantCommitTransaction);
    DEFINE_RPC_PROXY_METHOD(NMiniYT::NTablet::NApi, ParticipantAbortTransaction);
};

}; // namespace NMiniYT
