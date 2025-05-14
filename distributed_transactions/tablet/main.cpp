#include "coordinator.h"
#include "participant.h"

#include <distributed_transactions/common/enable_ipv4.h>

#include <yt/yt/core/concurrency/thread_pool.h>

#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/bus/tcp/server.h>
#include <yt/yt/core/rpc/bus/server.h>

#include <library/cpp/getopt/last_getopt.h>

#include <util/random/random.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, TabletLogger, "Tablet");
static constexpr auto& Logger = TabletLogger;

void RunServer(int port, int numThreads, ui64 id, bool useHLC, ui64 randomClockOffset, const TString& timestampProviderAddress, const TVector<TString>& otherTabletAddresses)
{
    EnableIPv4();

    auto busServer = NYT::NBus::CreateBusServer(NYT::NBus::TBusServerConfig::CreateTcp(port));
    auto rpcServer = NYT::NRpc::NBus::CreateBusServer(std::move(busServer));
    auto rpcPool = NYT::NConcurrency::CreateThreadPool(numThreads, "Tablet");

    auto storage = TMVCCStorage();
    auto transactions = TTransactionsMap();

    // random in range [-randomClockOffset, +randomClockOffset]
    auto clockOffset = static_cast<i64>(RandomNumber<ui64>(2 * randomClockOffset + 1)) - static_cast<i64>(randomClockOffset);

    auto clock = THybridLogicalClock(0, 0, clockOffset);

    if (useHLC) {
        auto coordinatorService = NYT::New<TCoordinatorServiceWithHLC>(id, rpcPool->GetInvoker(), otherTabletAddresses, storage, transactions, clock);
        auto participantService = NYT::New<TParticipantService>(rpcPool->GetInvoker(), storage, transactions, &clock);
        rpcServer->RegisterService(coordinatorService);
        rpcServer->RegisterService(participantService);
    } else {
        auto coordinatorService = NYT::New<TCoordinatorService>(id, rpcPool->GetInvoker(), timestampProviderAddress, otherTabletAddresses, storage, transactions);
        auto participantService = NYT::New<TParticipantService>(rpcPool->GetInvoker(), storage, transactions, nullptr);
        rpcServer->RegisterService(coordinatorService);
        rpcServer->RegisterService(participantService);
    }

    rpcServer->Start();

    YT_LOG_INFO("Tablet server is running on port %v", port);

    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::Max());
}

} // namespace NMiniYT

int main(int argc, char* argv[])
{
    using namespace NLastGetopt;

    TOpts opts;

    int port;
    opts.AddLongOption("port", "")
        .RequiredArgument("PORT")
        .StoreResult(&port);

    ui64 id;
    opts.AddLongOption("id", "")
        .RequiredArgument("ID")
        .StoreResult(&id);

    int numThreads;
    opts.AddLongOption("threads", "")
        .OptionalArgument("NUM_THREADS")
        .DefaultValue(4)
        .StoreResult(&numThreads);

    bool useHLC;
    opts.AddLongOption("use-hlc", "")
        .OptionalArgument("USE_HLC")
        .StoreTrue(&useHLC);

    ui64 randomClockOffsetNanoseconds;
    opts.AddLongOption("random-clock-offset-ns", "")
        .OptionalArgument("RANDOM_CLOCK_OFFSET_NS")
        .DefaultValue(200'000'000)  // 200 MS
        .StoreResult(&randomClockOffsetNanoseconds);

    TString timestampProviderAddress;
    opts.AddLongOption("ts-provider", "")
        .RequiredArgument("TS_PROVIDER_ADDR")
        .StoreResult(&timestampProviderAddress);

    TVector<TString> otherTabletAddresses;
    opts.AddLongOption("tablet", "")
        .AppendTo(&otherTabletAddresses);

    TOptsParseResult results(&opts, argc, argv);
    NMiniYT::RunServer(port, numThreads, id, useHLC, randomClockOffsetNanoseconds, timestampProviderAddress, otherTabletAddresses);

    return 0;
}
