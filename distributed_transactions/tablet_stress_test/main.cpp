#include <distributed_transactions/client/transaction_client.h>
#include <distributed_transactions/client/transaction_examples.h>

#include <distributed_transactions/common/enable_ipv4.h>

#include <yt/yt/core/concurrency/async_barrier.h>
#include <yt/yt/core/concurrency/thread_pool.h>
#include <yt/yt/core/profiling/timing.h>

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/random/random.h>
#include <util/random/shuffle.h>
#include <util/system/file.h>
#include <util/stream/file.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, RpcClientLogger, "RpcClient");
static constexpr auto& Logger = RpcClientLogger;

TVector<TKey> GenerateKeys(size_t numKeys) {
    auto keys = TVector<TKey>();
    keys.reserve(numKeys);
    for (size_t i = 0; i < numKeys; ++i) {
        keys.push_back("C" + ToString(i));
    }

    return keys;
}

namespace NTransfersTest {

    void ReadCheck(const TVector<TValue>& values) {
        i64 sum = 0;
        for (const auto& value : values) {
            sum += AsInt(value);
        }
        if (sum != 0) {
            YT_LOG_FATAL("consistency violated: sum = %v, values = %v", sum, values);
        }
    }

    class TTransactionGenerator {
    public:
        TTransactionGenerator(const TVector<TKey>& keys) {
            Y_ENSURE(keys.size() >= 20);

            TransactionKeys_ = keys;
            const auto numKeysInTransaction = (1 + RandomNumber<size_t>(keys.size() / 20)) * 2;
            PartialShuffle(TransactionKeys_.begin(), TransactionKeys_.end(), numKeysInTransaction);
            TransactionKeys_.resize(numKeysInTransaction);

            Deltas_ = TVector<i64>();
            Deltas_.reserve(numKeysInTransaction / 2);
            for (size_t j = 0; j < numKeysInTransaction / 2; ++j) {
                Deltas_.push_back(RandomNumber<ui64>(100));
            }
        }

        ETransactionResult RunTransaction(const TTransactionClient& client) {
            return TransferTransaction(client, TransactionKeys_, Deltas_);
        }

        size_t GetNumKeysInTransaction() {
            return TransactionKeys_.size();
        }

    private:
        TVector<TKey> TransactionKeys_;
        TVector<i64> Deltas_;
    };

}  // namespace NTransfersTest

namespace NAdditionsTest {

    void ReadCheck(const TVector<TValue>& values) {
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
    }

    class TTransactionGenerator {
    public:
        TTransactionGenerator(const TVector<TKey>& keys)
            : Keys_(keys)
        {
            Deltas_ = TVector<i64>(Keys_.size(), static_cast<i64>(1 + RandomNumber<ui64>(100)));
        }

        ETransactionResult RunTransaction(const TTransactionClient& client) {
            return AddTransaction(client, Keys_, Deltas_);
        }

        size_t GetNumKeysInTransaction() {
            return Keys_.size();
        }

    private:
        const TVector<TKey>& Keys_;
        TVector<i64> Deltas_;
    };
}  // namespace NAdditionsTest

template <typename TTransactionGenerator>
void RunStressTest(void (*readCheck)(const TVector<TValue>&), const TVector<TAddress>& tablets, size_t numKeys, size_t numWrites, bool isReadBlocking, size_t readFrequency, size_t numThreads, TString testName) {
    EnableIPv4();

    YT_LOG_INFO("Connecting to tablets...");

    const auto client = TTransactionClient(tablets);

    YT_LOG_INFO("Done");

    const auto keys = GenerateKeys(numKeys);

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

    for (size_t i = 0; i < numWrites; ++i) {
        {
            auto cookie = barrier.Insert();

            pool->GetInvoker()->Invoke(BIND([&, cookie = std::move(cookie)](){
                auto transactionGenerator = TTransactionGenerator(keys);

                NYT::NProfiling::TWallTimer transactionTimer;
                transactionTimer.Start();

                while (transactionGenerator.RunTransaction(client) != ETransactionResult::OK) {
                    failedWrites.fetch_add(1);
                    NYT::NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::MilliSeconds(RandomNumber<size_t>(100)));
                    transactionTimer.Restart();
                }

                numKeysUpdated.fetch_add(transactionGenerator.GetNumKeysInTransaction());
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
                while (ReadOnlyTransaction(client, keys, isReadBlocking, readCheck) != ETransactionResult::OK) {
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

    const auto elapsedTime = timer.GetElapsedTime();

    auto keyLocations = TVector<TString>();
    for (const auto& key : keys) {
        keyLocations.push_back(client.GetTabletAddressForKey(key));
    }

    auto result = ReadOnlyTransaction(client, keys, true, [&](const auto& values){
        YT_LOG_INFO("keys: %v", keys);
        YT_LOG_INFO("values: %v", values);
        YT_LOG_INFO("tablets: %v", keyLocations);
        readCheck(values);
    });

    if (result != ETransactionResult::OK) {
        YT_LOG_FATAL("%v", static_cast<int>(result));
    }

    const auto averageUpdatedKeys = static_cast<double>(numKeysUpdated) / successfulWrites;
    const auto averageWriteLatency = static_cast<double>(totalWriteLatency) / successfulWrites;
    const auto averageReadLatency = static_cast<double>(totalReadLatency) / successfulReads;
    const auto transactionsPerSecond = (successfulWrites + successfulReads) * 1000000 / elapsedTime.MicroSeconds();

    const auto failedWritesRatio = static_cast<double>(failedWrites) / successfulWrites;
    const auto failedReadsRatio = static_cast<double>(failedReads) / successfulReads;

    YT_LOG_INFO("Successfull writes: %v, failed writes: %v", successfulWrites, failedWrites);
    YT_LOG_INFO("Successfull reads: %v, failed reads: %v", successfulReads, failedReads);
    YT_LOG_INFO("Average successful updates number of keys: %v", averageUpdatedKeys);
    YT_LOG_INFO("Average (successful) write transaction latency: %v us", averageWriteLatency);
    YT_LOG_INFO("Average (successful) read transaction latency: %v us", averageReadLatency);
    YT_LOG_INFO("Failed writes ratio %v", failedWritesRatio);
    YT_LOG_INFO("Failed reads ratio %v", failedReadsRatio);
    YT_LOG_INFO("TPS: %v", transactionsPerSecond);
    YT_LOG_INFO("Elapsed time: %v", elapsedTime);

    auto file = TFile("metrics/metrics.csv", OpenAlways | WrOnly | ForAppend);
    auto output = TFileOutput(file);
    output << Join(",", testName, successfulWrites, failedWrites, successfulReads, failedReads, averageUpdatedKeys, averageWriteLatency, averageReadLatency, failedWritesRatio, failedReadsRatio, transactionsPerSecond) << "\n";
}

} // namespace NMiniYT

int main(int argc, char* argv[])
{
    using NMiniYT::Logger;

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

    size_t numWrites;
    opts.AddLongOption("writes", "")
        .OptionalArgument("NUM_WRITES")
        .DefaultValue(100)
        .StoreResult(&numWrites);

    bool isReadBlocking;
    opts.AddLongOption("blocking-read", "")
        .OptionalArgument("BLOCKING_READ")
        .DefaultValue(false)
        .StoreTrue(&isReadBlocking);

    size_t readFrequency;
    opts.AddLongOption("reads", "")
        .OptionalArgument("NUM_READS")
        .DefaultValue(10)  // there will be one read per 10 writes
        .StoreResult(&readFrequency);

    size_t numThreads;
    opts.AddLongOption("threads", "")
        .OptionalArgument("NUM_THREADS")
        .DefaultValue(4)
        .StoreResult(&numThreads);

    TString testName;
    opts.AddLongOption("name", "")
        .OptionalArgument("NAME")
        .DefaultValue("")
        .StoreResult(&testName);

    TString testType;
    opts.AddLongOption("type", "")
        .OptionalArgument("TYPE")
        .DefaultValue("transfers")
        .StoreResult(&testType);

    NLastGetopt::TOptsParseResult results(&opts, argc, argv);

    testName = Join(",", testType, testName);

    if (testType == "transfers") {
        NMiniYT::RunStressTest<NMiniYT::NTransfersTest::TTransactionGenerator>(
            NMiniYT::NTransfersTest::ReadCheck, tablets, numKeys, numWrites, isReadBlocking, readFrequency, numThreads, testName
        );
    } else if (testType == "additions") {
        NMiniYT::RunStressTest<NMiniYT::NAdditionsTest::TTransactionGenerator>(
            NMiniYT::NAdditionsTest::ReadCheck, tablets, numKeys, numWrites, isReadBlocking, readFrequency, numThreads, testName
        );
    } else {
        YT_LOG_FATAL("invalid test type: %v", testType);
    }

    return 0;
}
