#pragma once

#include <distributed_transactions/tablet/proto/participant_api.pb.h>

#include <distributed_transactions/tablet/proxy/rpc_proxy.h>
#include <distributed_transactions/tablet/common/mvcc_storage.h>
#include <distributed_transactions/tablet/common/transactions_map.h>

#include <distributed_transactions/tablet/hlc/hybrid_logical_clock.h>

#include <yt/yt/core/rpc/service_detail.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, ParticipantLogger, "Participant");

class TParticipantServiceBase
{
public:
    TParticipantServiceBase(TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock* clock = nullptr);

    void OnReceiveMessage(const google::protobuf::Message& message);
    void OnSendMessage(google::protobuf::Message& message);

    void DoParticipantPrepareTransaction(const NTablet::NApi::TReqParticipantPrepareTransaction& request, NTablet::NApi::TRspParticipantPrepareTransaction& response);
    void DoParticipantCommitTransaction(const NTablet::NApi::TReqParticipantCommitTransaction& request, NTablet::NApi::TRspParticipantCommitTransaction& response);
    void DoParticipantAbortTransaction(const NTablet::NApi::TReqParticipantAbortTransaction& request);

private:
    TMVCCStorage& Storage_;
    TTransactionsMap& Transactions_;
    THybridLogicalClock* Clock_;
};

class TParticipantService : public TParticipantServiceBase, public NYT::NRpc::TServiceBase
{
public:
    TParticipantService(NYT::IInvokerPtr invoker, TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock* clock = nullptr);

    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, ParticipantPrepareTransaction);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, ParticipantCommitTransaction);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, ParticipantAbortTransaction);
};

}  // namespace NMiniYT
