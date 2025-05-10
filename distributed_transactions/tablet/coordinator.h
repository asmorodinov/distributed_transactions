#pragma once

#include <distributed_transactions/tablet/proto/coordinator_api.pb.h>

#include <distributed_transactions/tablet/proxy/rpc_proxy.h>
#include <distributed_transactions/tablet/common/mvcc_storage.h>
#include <distributed_transactions/tablet/common/transactions_map.h>

#include <distributed_transactions/tablet/hlc/hybrid_logical_clock.h>
#include <distributed_transactions/timestamp_provider/proxy/rpc_proxy.h>

#include <yt/yt/core/rpc/service_detail.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, CoordinatorLogger, "Coordinator");

class ICoordinatorService : public NYT::NRpc::TServiceBase
{
public:
    ICoordinatorService(ui64 id, NYT::IInvokerPtr invoker, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions);
    virtual ~ICoordinatorService() = default;

    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, StartTransaction);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, ReadRow);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, WriteIntent);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, ClientCommitTransaction);
    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTablet::NApi, AbortTransaction);

protected:
    virtual TTimestamp GetTimestamp() = 0;

    virtual void OnReceiveMessage(const google::protobuf::Message& message);
    virtual void OnSendMessage(google::protobuf::Message& message);

private:
    void AddParticipantProxy(const TString& address);

private:
    ui64 ID_;
    TMVCCStorage& Storage_;
    TTransactionsMap& Transactions_;

    THashMap<TString, TParticipantProxy> TabletProxies_;
};

class TCoordinatorService : public ICoordinatorService
{
public:
    TCoordinatorService(ui64 id, NYT::IInvokerPtr invoker, const TString& timestampProviderAddress, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions);

protected:
    TTimestamp GetTimestamp() override;

private:
    static TTimestampProviderProxy CreateTimestampProvider(const TString& address);

private:
    TTimestampProviderProxy TimestampProviderProxy_;
};

class TCoordinatorServiceWithHLC : public ICoordinatorService
{
public:
    TCoordinatorServiceWithHLC(ui64 id, NYT::IInvokerPtr invoker, const TVector<TString>& otherParticipantAddresses, TMVCCStorage& storage, TTransactionsMap& transactions, THybridLogicalClock& clock);

protected:
    TTimestamp GetTimestamp() override;

    void OnReceiveMessage(const google::protobuf::Message& message) override;
    void OnSendMessage(google::protobuf::Message& message) override;

private:
    THybridLogicalClock& Clock_;
};

} // namespace NMiniYT
