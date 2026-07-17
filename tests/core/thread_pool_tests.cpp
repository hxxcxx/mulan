#include <mulan/core/concurrency/thread_pool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mulan::core {
namespace {

using namespace std::chrono_literals;

TEST(ThreadPoolTests, RejectsZeroWorkers) {
    EXPECT_THROW(ThreadPool(0), std::invalid_argument);
}

TEST(ThreadPoolTests, ExecutesTasksAndReturnsTheirResults) {
    ThreadPool pool(2);

    auto integerResult = pool.submit([] { return 42; });
    auto voidResult = pool.submit([] {});
    auto moveOnlyResult = pool.submit([value = std::make_unique<int>(7)] { return *value; });

    EXPECT_EQ(integerResult.get(), 42);
    EXPECT_NO_THROW(voidResult.get());
    EXPECT_EQ(moveOnlyResult.get(), 7);
}

TEST(ThreadPoolTests, PropagatesTaskExceptionThroughFuture) {
    ThreadPool pool(1);
    auto result = pool.submit([]() -> int { throw std::runtime_error("expected task failure"); });
    EXPECT_THROW(result.get(), std::runtime_error);
}

TEST(ThreadPoolTests, ExecutesTasksConcurrently) {
    ThreadPool pool(2);
    std::promise<void> release;
    const std::shared_future<void> releaseFuture = release.get_future().share();
    std::promise<void> firstStarted;
    std::promise<void> secondStarted;
    auto firstStartedFuture = firstStarted.get_future();
    auto secondStartedFuture = secondStarted.get_future();

    auto first = pool.submit([&] {
        firstStarted.set_value();
        releaseFuture.wait();
    });
    auto second = pool.submit([&] {
        secondStarted.set_value();
        releaseFuture.wait();
    });

    const bool bothStarted = firstStartedFuture.wait_for(2s) == std::future_status::ready &&
                             secondStartedFuture.wait_for(2s) == std::future_status::ready;
    release.set_value();
    first.get();
    second.get();
    EXPECT_TRUE(bothStarted);
}

TEST(ThreadPoolTests, ExecutesEveryAcceptedTaskExactlyOnce) {
    ThreadPool pool(4);
    std::atomic_size_t executionCount = 0;
    std::vector<std::future<void>> results;
    results.reserve(256);
    for (size_t i = 0; i < 256; ++i) {
        results.push_back(pool.submit([&executionCount] { executionCount.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (auto& result : results)
        result.get();
    EXPECT_EQ(executionCount.load(std::memory_order_relaxed), 256u);
}

TEST(ThreadPoolTests, MultipleInstancesExecuteIndependently) {
    ThreadPool firstPool(1);
    ThreadPool secondPool(2);

    auto first = firstPool.submit([] { return 11; });
    auto second = secondPool.submit([] { return 29; });

    EXPECT_EQ(firstPool.workerCount(), 1u);
    EXPECT_EQ(secondPool.workerCount(), 2u);
    EXPECT_EQ(first.get(), 11);
    EXPECT_EQ(second.get(), 29);
}

TEST(ThreadPoolTests, DestructorDrainsQueuedTasks) {
    std::atomic_size_t executionCount = 0;
    std::future<void> first;
    std::future<void> second;
    std::promise<void> release;
    const std::shared_future<void> releaseFuture = release.get_future().share();
    std::promise<void> firstStarted;
    auto firstStartedFuture = firstStarted.get_future();

    {
        ThreadPool pool(1);
        first = pool.submit([&] {
            firstStarted.set_value();
            releaseFuture.wait();
            executionCount.fetch_add(1, std::memory_order_relaxed);
        });
        second = pool.submit([&] { executionCount.fetch_add(1, std::memory_order_relaxed); });

        const bool firstDidStart = firstStartedFuture.wait_for(2s) == std::future_status::ready;
        release.set_value();
        EXPECT_TRUE(firstDidStart);
    }

    EXPECT_NO_THROW(first.get());
    EXPECT_NO_THROW(second.get());
    EXPECT_EQ(executionCount.load(std::memory_order_relaxed), 2u);
}

}  // namespace
}  // namespace mulan::core
