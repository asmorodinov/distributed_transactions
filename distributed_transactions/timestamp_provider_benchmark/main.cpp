#include <distributed_transactions/timestamp_provider/proto/api.pb.h>
#include <distributed_transactions/timestamp_provider/proxy/rpc_proxy.h>
#include <distributed_transactions/common/format_timestamp.h>

#include <yt/yt/core/bus/tcp/client.h>
#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/rpc/bus/channel.h>
#include <yt/yt/core/profiling/timing.h>

#include <library/cpp/getopt/last_getopt.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, RpcClientLogger, "RpcClient");
static constexpr auto& Logger = RpcClientLogger;

void RunBenchmark(int port, int iterations)
{
    auto client = NYT::NBus::CreateBusClient(NYT::NBus::TBusClientConfig::CreateTcp(NYT::Format("localhost:%v", port)));
    auto channel = NYT::NRpc::NBus::CreateBusChannel(client);
    TTimestampProviderProxy proxy(channel);
    proxy.SetDefaultAcknowledgementTimeout(std::nullopt);

    NYT::NProfiling::TWallTimer timer;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        auto message = ToString(iteration);
        auto req = proxy.GenerateTimestamp();
        auto rsp = NYT::NConcurrency::WaitFor(req->Invoke())
            .ValueOrThrow();

        ui64 timestamp = rsp->GetTimestamp();
        YT_LOG_INFO("Got timestamp %v", NUtil::FormatTimestamp(timestamp));
    }

    YT_LOG_INFO("Processed %v requests in %v", iterations, timer.GetElapsedTime());
}

} // namespace NMiniYT

int main(int argc, char* argv[])
{
    NLastGetopt::TOpts opts;

    int port;
    opts.AddLongOption("port", "")
        .RequiredArgument("PORT")
        .StoreResult(&port);

    int iterations = 1;
    opts.AddLongOption("iterations", "")
        .OptionalArgument("ITERATIONS")
        .StoreResult(&iterations);

    NLastGetopt::TOptsParseResult results(&opts, argc, argv);
    NMiniYT::RunBenchmark(port, iterations);

    return 0;
}
