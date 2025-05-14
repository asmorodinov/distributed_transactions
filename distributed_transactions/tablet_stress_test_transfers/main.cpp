#include <distributed_transactions/client/transaction_client.h>
#include <distributed_transactions/client/transaction_examples.h>

#include <distributed_transactions/common/enable_ipv4.h>

#include <yt/yt/core/concurrency/async_barrier.h>
#include <yt/yt/core/concurrency/thread_pool.h>

#include <util/random/random.h>
#include <util/random/shuffle.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, RpcClientLogger, "RpcClient");
static constexpr auto& Logger = RpcClientLogger;

void RunStressTest(const TVector<TAddress>& tablets, size_t numKeys, size_t numTransfers, size_t readFrequency, size_t numThreads) {
    EnableIPv4();

    Y_ENSURE(numKeys >= 20);

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

    auto numKeysUpdated = std::atomic<size_t>(0);

    auto totalWriteLatency = std::atomic<ui64>(0);
    auto totalReadLatency = std::atomic<ui64>(0);

    NYT::NProfiling::TWallTimer timer;

    YT_LOG_INFO("Executing transactions...");

    auto readCheck = [](const auto& values){
        i64 sum = 0;
        for (const auto& value : values) {
            sum += AsInt(value);
        }
        Y_ENSURE(sum == 0);
    };

    for (size_t i = 0; i < numTransfers; ++i) {
        {
            auto cookie = barrier.Insert();

            pool->GetInvoker()->Invoke(BIND([&, cookie = std::move(cookie)](){
                auto transactionKeys = keys;
                const auto numKeysInTransaction = (1 + RandomNumber<size_t>(numKeys / 20)) * 2;
                PartialShuffle(transactionKeys.begin(), transactionKeys.end(), numKeysInTransaction);
                transactionKeys.resize(numKeysInTransaction);

                auto deltas = TVector<i64>();
                deltas.reserve(numKeysInTransaction / 2);
                for (size_t j = 0; j < numKeysInTransaction / 2; ++j) {
                    deltas.push_back(RandomNumber<ui64>(100));
                }

                NYT::NProfiling::TWallTimer transactionTimer;
                transactionTimer.Start();
                while (TransferTransaction(client, transactionKeys, deltas) != ETransactionResult::OK) {
                    failedWrites.fetch_add(1);
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MilliSeconds(RandomNumber<size_t>(100)));
                    transactionTimer.Restart();
                }

                numKeysUpdated.fetch_add(numKeysInTransaction);
                successfulWrites.fetch_add(1);
                totalWriteLatency.fetch_add(transactionTimer.GetElapsedTime().MicroSeconds());
                barrier.Remove(cookie);
            }));
        }

        if (i % readFrequency == 0) {
            auto cookie = barrier.Insert();

            pool->GetInvoker()->Invoke(BIND([&, cookie = std::move(cookie)](){
                NYT::NProfiling::TWallTimer transactionTimer;

                transactionTimer.Start();
                while (ReadOnlyTransaction(client, keys, readCheck) != ETransactionResult::OK) {
                    failedReads.fetch_add(1);
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MilliSeconds(RandomNumber<size_t>(10)));
                    transactionTimer.Restart();
                }
                successfulReads.fetch_add(1);
                totalReadLatency.fetch_add(transactionTimer.GetElapsedTime().MicroSeconds());
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

    auto result = ReadOnlyTransaction(client, keys, [&](const auto& values){
        YT_LOG_INFO("keys: %v", keys);
        YT_LOG_INFO("values: %v", values);
        YT_LOG_INFO("tablets: %v", keyLocations);
        readCheck(values);
    });

    YT_LOG_INFO("%v", static_cast<int>(result));

    YT_LOG_INFO("Successfull writes: %v, failed writes: %v", successfulWrites.load(), failedWrites.load());
    YT_LOG_INFO("Successfull reads: %v, failed reads: %v", successfulReads.load(), failedReads.load());
    YT_LOG_INFO("Average successful updates number of keys: %v", static_cast<double>(numKeysUpdated.load()) / successfulWrites.load());
    YT_LOG_INFO("Average (successful) write transaction latency: %v us", static_cast<double>(totalWriteLatency.load()) / successfulWrites.load());
    YT_LOG_INFO("Average (successful) read transaction latency: %v us", static_cast<double>(totalReadLatency.load()) / successfulReads.load());

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

    size_t numTransfers;
    opts.AddLongOption("transfers", "")
        .OptionalArgument("NUM_TRANSFERS")
        .DefaultValue(100)
        .StoreResult(&numTransfers);

    size_t readFrequency;
    opts.AddLongOption("reads", "")
        .OptionalArgument("NUM_READS")
        .DefaultValue(10)  // there will be one read per 10 transfers
        .StoreResult(&readFrequency);

    size_t numThreads;
    opts.AddLongOption("threads", "")
        .OptionalArgument("NUM_THREADS")
        .DefaultValue(4)
        .StoreResult(&numThreads);

    NLastGetopt::TOptsParseResult results(&opts, argc, argv);

    NMiniYT::RunStressTest(tablets, numKeys, numTransfers, readFrequency, numThreads);

    return 0;
}
