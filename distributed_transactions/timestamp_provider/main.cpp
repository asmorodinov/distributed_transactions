#include <distributed_transactions/timestamp_provider/proto/api.pb.h>
#include <distributed_transactions/timestamp_provider/proxy/rpc_proxy.h>

#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/bus/tcp/server.h>
#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/rpc/bus/server.h>
#include <yt/yt/core/rpc/service_detail.h>

#include <library/cpp/getopt/last_getopt.h>

#include <chrono>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, TimestampProviderLogger, "TimestampProvider");
static constexpr auto& Logger = TimestampProviderLogger;

class TTimestampProviderService
    : public NYT::NRpc::TServiceBase
{
public:
    explicit TTimestampProviderService(NYT::IInvokerPtr invoker)
        : TServiceBase(
            std::move(invoker),
            TTimestampProviderProxy::GetDescriptor(),
            TimestampProviderLogger())
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GenerateTimestamp)
            .SetQueueSizeLimit(100'000'000));
    }

    static ui64 GetCurrentTimestamp()
    {
        const auto duration = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }

    DECLARE_RPC_SERVICE_METHOD(NMiniYT::NTimestampProvider::NApi, GenerateTimestamp);

private:
    ui64 LastTimestamp_ = 0;
};

DEFINE_RPC_SERVICE_METHOD(TTimestampProviderService, GenerateTimestamp)
{
    while (true) {
        auto currentTimestamp = GetCurrentTimestamp();
        if (currentTimestamp <= LastTimestamp_) {
            // clock went backwards, or too fast timestamp request rate
            ui64 sleepTime = (LastTimestamp_ - currentTimestamp) / 1000 + 1;
            NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MicroSeconds(sleepTime));
        } else {
            LastTimestamp_ = currentTimestamp;
            response->SetTimestamp(currentTimestamp);
            break;
        }
    }

    context->Reply();
}

void RunServer(int port)
{
    auto busServer = NYT::NBus::CreateBusServer(NYT::NBus::TBusServerConfig::CreateTcp(port));
    auto rpcServer = NYT::NRpc::NBus::CreateBusServer(std::move(busServer));
    auto timestampQueue = NYT::New<NYT::NConcurrency::TActionQueue>("Timestamp");

    rpcServer->RegisterService(NYT::New<TTimestampProviderService>(timestampQueue->GetInvoker()));
    rpcServer->Start();

    YT_LOG_INFO("Timestamp provider is running on port %v", port);

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

    TOptsParseResult results(&opts, argc, argv);
    NMiniYT::RunServer(port);

    return 0;
}
