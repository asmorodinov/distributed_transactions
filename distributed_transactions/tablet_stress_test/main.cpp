#include <distributed_transactions/client/transaction_client.h>
#include <distributed_transactions/client/transaction_examples.h>

#include <distributed_transactions/common/enable_ipv4.h>

#include <yt/yt/core/concurrency/async_barrier.h>
#include <yt/yt/core/concurrency/thread_pool.h>
#include <yt/yt/core/profiling/timing.h>

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/random/random.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, RpcClientLogger, "RpcClient");
static constexpr auto& Logger = RpcClientLogger;

void RunStressTest(const TVector<TAddress>& tablets, size_t numKeys, size_t numIncrements, bool isReadBlocking, size_t readFrequency, size_t numThreads) {
    EnableIPv4();

    YT_LOG_INFO("Connecting to tablets...");

    const auto client = TTransactionClient(tablets);

    YT_LOG_INFO("Done");

    auto keys = TVector<TString>();
    keys.reserve(numKeys);
    for (size_t i = 0; i < numKeys; ++i) {
        keys.push_back("C" + ToString(i));
    }

    const auto deltas = TVector<i64>(numKeys, 1);

    auto pool = NYT::NConcurrency::CreateThreadPool(numThreads, "ClientPool");
    NYT::NConcurrency::TAsyncBarrier barrier;

    auto successfulWrites = std::atomic<size_t>(0);
    auto failedWrites = std::atomic<size_t>(0);

    auto successfulReads = std::atomic<size_t>(0);
    auto failedReads = std::atomic<size_t>(0);

    NYT::NProfiling::TWallTimer timer;

    YT_LOG_INFO("Executing transactions...");

    auto readCheck = [](const auto& values){
        bool isCorrect = true;
        for (const auto& value : values) {
            if (value != values[0]) {
                isCorrect = false;
                break;
            }
        }
        if (!isCorrect) {
            YT_LOG_FATAL("consistency violated: %v", values);
        }
    };

    for (size_t i = 0; i < numIncrements; ++i) {
        {
            auto cookie = barrier.Insert();

            pool->GetInvoker()->Invoke(BIND([&, cookie = std::move(cookie)](){
                while (AddTransaction(client, keys, deltas) != ETransactionResult::OK) {
                    failedWrites.fetch_add(1);
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MilliSeconds(RandomNumber<size_t>(100)));
                }
                successfulWrites.fetch_add(1);
                barrier.Remove(cookie);
            }));
        }

        if (i % readFrequency == 0) {
            auto cookie = barrier.Insert();

            pool->GetInvoker()->Invoke(BIND([&, cookie = std::move(cookie)](){
                while (ReadOnlyTransaction(client, keys, isReadBlocking, readCheck) != ETransactionResult::OK) {
                    failedReads.fetch_add(1);
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MilliSeconds(RandomNumber<size_t>(10)));
                }
                successfulReads.fetch_add(1);
                barrier.Remove(cookie);
            }));
        }
    }

    auto res = NYT::NConcurrency::WaitFor(barrier.GetBarrierFuture());
    YT_VERIFY(res.IsOK());

    auto keyLocations = TVector<TString>();
    for (const auto& key : keys) {
        keyLocations.push_back(client.GetTabletAddressForKey(key));
    }

    auto result = ReadOnlyTransaction(client, keys, true, [&](const auto& values){
        YT_LOG_INFO("keys: %v", keys);
        YT_LOG_INFO("values: %v", values);
        YT_LOG_INFO("tablets: %v", keyLocations);
    });
    YT_LOG_INFO("%v", static_cast<int>(result));

    YT_LOG_INFO("Successfull writes: %v, failed writes: %v", successfulWrites.load(), failedWrites.load());
    YT_LOG_INFO("Successfull reads: %v, failed reads: %v", successfulReads.load(), failedReads.load());
    YT_LOG_INFO("Elapsed time: %v", timer.GetElapsedTime());
}

} // namespace NMiniYT

int main(int argc, char* argv[])
{
    NLastGetopt::TOpts opts;

    TVector<NMiniYT::TAddress> tablets;
    opts.AddLongOption("tablet", "")
        .RequiredArgument("TABLET")
        .AppendTo(&tablets);

    size_t numKeys;
    opts.AddLongOption("keys", "")
        .OptionalArgument("NUM_KEYS")
        .DefaultValue(10)
        .StoreResult(&numKeys);

    size_t numIncrements;
    opts.AddLongOption("increments", "")
        .OptionalArgument("NUM_INCREMENTS")
        .DefaultValue(100)
        .StoreResult(&numIncrements);

    bool isReadBlocking;
    opts.AddLongOption("blocking-read", "")
        .OptionalArgument("BLOCKING_READ")
        .DefaultValue(false)
        .StoreTrue(&isReadBlocking);

    size_t readFrequency;
    opts.AddLongOption("reads", "")
        .OptionalArgument("NUM_READS")
        .DefaultValue(5)  // there will be one read per 5 increments
        .StoreResult(&readFrequency);

    size_t numThreads;
    opts.AddLongOption("threads", "")
        .OptionalArgument("NUM_THREADS")
        .DefaultValue(4)
        .StoreResult(&numThreads);

    NLastGetopt::TOptsParseResult results(&opts, argc, argv);

    NMiniYT::RunStressTest(tablets, numKeys, numIncrements, isReadBlocking, readFrequency, numThreads);

    return 0;
}
