/**
 * @file render_thread_tests.cpp
 * @brief RenderThread 共享策略与 RenderChannelState 资源回执测试。
 * @author hxxcxx
 * @date 2026-07-17
 *
 * 纯状态机测试，不初始化 GPU、窗口或真实 RenderExecutor。
 */

#include "runtime/detail/render_channel_state.h"
#include "runtime/detail/render_thread.h"

#include <mulan/rhi/engine_error_code.h>

#include <gtest/gtest.h>

#include <vector>

using namespace mulan::view::detail;

TEST(RenderChannelStateTests, RepeatedPendingBatchQueuesOnceAndProducesOneAck) {
    RenderChannelState state;

    EXPECT_TRUE(state.beginResourceBatch(41));
    EXPECT_FALSE(state.beginResourceBatch(41));

    state.completeResourceBatch(41);
    ASSERT_EQ(state.takeCompletedResourceBatch(), 41u);
    EXPECT_FALSE(state.takeCompletedResourceBatch().has_value());
}

TEST(RenderChannelStateTests, LatestCompletionCoalescesOlderAcks) {
    RenderChannelState state;

    ASSERT_TRUE(state.beginResourceBatch(11));
    state.completeResourceBatch(11);
    ASSERT_TRUE(state.beginResourceBatch(12));
    state.completeResourceBatch(12);

    ASSERT_EQ(state.takeCompletedResourceBatch(), 12u);
    EXPECT_FALSE(state.takeCompletedResourceBatch().has_value());
}

TEST(RenderChannelStateTests, ResourceInvalidationAllowsSameBatchToQueueAgain) {
    RenderChannelState state;

    EXPECT_TRUE(state.beginResourceBatch(7));
    EXPECT_FALSE(state.beginResourceBatch(7));
    state.invalidateResourceBatch();
    EXPECT_TRUE(state.beginResourceBatch(7));
}

TEST(RenderChannelStateTests, FailureIsPersistentAndSuppressesPendingAndFutureAcks) {
    RenderChannelState state;
    ASSERT_TRUE(state.beginResourceBatch(9));
    state.completeResourceBatch(9);

    const mulan::Error failure = mulan::Error::make(mulan::ErrorCode::Internal, "resource upload failed");
    state.fail(failure);
    state.completeResourceBatch(9);

    EXPECT_FALSE(state.takeCompletedResourceBatch().has_value());
    ASSERT_TRUE(state.failure().has_value());
    EXPECT_EQ(state.failure()->message, failure.message);
}

TEST(RenderChannelStateTests, FirstFailureRemainsTheRootCause) {
    RenderChannelState state;
    const mulan::Error first = mulan::Error::make(mulan::ErrorCode::Internal, "first failure");
    const mulan::Error later = mulan::Error::make(mulan::ErrorCode::InvalidArg, "later failure");

    state.fail(first);
    state.fail(later);

    ASSERT_TRUE(state.failure().has_value());
    EXPECT_EQ(state.failure()->message, first.message);
}

TEST(RenderThreadTests, CompatibleThreadedViewsReuseOneThread) {
    mulan::view::ViewConfig config;
    config.backend = mulan::engine::GraphicsBackend::Vulkan;
    auto first = RenderThread::acquire(config);
    auto second = RenderThread::acquire(config);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->get(), second->get());
    EXPECT_EQ((*first)->stats().threadId, (*second)->stats().threadId);

    config.msaa = mulan::engine::RenderConfig::MSAALevel::x8;
    auto incompatible = RenderThread::acquire(config);
    ASSERT_TRUE(incompatible);
    EXPECT_NE(first->get(), incompatible->get());
}

TEST(RenderThreadTests, OpenGLContextsAlwaysUseIndependentThreads) {
    mulan::view::ViewConfig config;
    config.backend = mulan::engine::GraphicsBackend::OpenGL;
    auto first = RenderThread::acquire(config);
    auto second = RenderThread::acquire(config);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(first->get(), second->get());
}

TEST(RenderThreadTests, FairCursorVisitsEveryContinuouslyReadyChannel) {
    FairChannelCursor cursor;
    std::vector<size_t> selected;
    for (size_t iteration = 0; iteration < 9; ++iteration) {
        const size_t index = cursor.start(3);
        selected.push_back(index);
        cursor.selected(index, 3);
    }
    EXPECT_EQ(selected, (std::vector<size_t>{ 0, 1, 2, 0, 1, 2, 0, 1, 2 }));
}

TEST(RenderThreadTests, DeviceFailurePoisonsTheOldThreadAndForcesReplacement) {
    mulan::view::ViewConfig config;
    config.backend = mulan::engine::GraphicsBackend::Vulkan;
    auto failedThread = RenderThread::acquire(config);
    ASSERT_TRUE(failedThread);
    const uint64_t failedId = (*failedThread)->stats().threadId;

    const mulan::Error failure =
            mulan::engine::makeError(mulan::engine::EngineErrorCode::DeviceLost, "injected shared device loss");
    (*failedThread)->injectFailureForTesting(failure);
    (*failedThread)->injectFailureForTesting(failure);
    EXPECT_EQ((*failedThread)->state(), RenderThreadState::Failed);
    EXPECT_EQ((*failedThread)->stats().failureBroadcastCount, 1u);
    EXPECT_FALSE((*failedThread)->submitFrame(999, mulan::view::RenderSubmission{}));
    EXPECT_EQ((*failedThread)->stats().rejectedWorkCount, 1u);

    auto replacement = RenderThread::acquire(config);
    ASSERT_TRUE(replacement);
    EXPECT_NE((*replacement)->stats().threadId, failedId);
    EXPECT_EQ((*replacement)->state(), RenderThreadState::Healthy);
}

TEST(RenderChannelStateTests, ThousandsOfCompletedBatchesKeepOnlyTheLatestAck) {
    RenderChannelState state;
    constexpr uint64_t batchCount = 10000;
    for (uint64_t batch = 1; batch <= batchCount; ++batch) {
        ASSERT_TRUE(state.beginResourceBatch(batch));
        state.completeResourceBatch(batch);
    }

    ASSERT_EQ(state.takeCompletedResourceBatch(), batchCount);
    EXPECT_FALSE(state.takeCompletedResourceBatch().has_value());
    EXPECT_FALSE(state.failure().has_value());
}
