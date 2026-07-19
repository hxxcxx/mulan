/**
 * @file render_thread_tests.cpp
 * @brief 渲染线程共享配置与 RenderChannelState 资源回执测试。
 * @author hxxcxx
 * @date 2026-07-17
 *
 * 纯状态机测试，不初始化 GPU、窗口或真实 RenderExecutor。
 */

#include "runtime/detail/render_channel_state.h"
#include "runtime/detail/render_runtime_config.h"

#include <gtest/gtest.h>

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

TEST(RenderDeviceConfigTests, CompatibleThreadedViewsCanShareOneThread) {
    mulan::view::ViewConfig config;
    config.backend = mulan::engine::GraphicsBackend::Vulkan;
    const RenderDeviceConfig first = RenderDeviceConfig::fromView(config);
    const RenderDeviceConfig second = RenderDeviceConfig::fromView(config);

    EXPECT_TRUE(first.sharesExecutionThreadWith(second));
}

TEST(RenderDeviceConfigTests, OpenGLContextsCannotShareExecutionThreads) {
    mulan::view::ViewConfig config;
    config.backend = mulan::engine::GraphicsBackend::OpenGL;
    const RenderDeviceConfig first = RenderDeviceConfig::fromView(config);
    const RenderDeviceConfig second = RenderDeviceConfig::fromView(config);

    EXPECT_FALSE(first.sharesExecutionThreadWith(second));
}

TEST(RenderDeviceConfigTests, SurfaceOnlyConfigurationDoesNotSplitExecutionThread) {
    mulan::view::ViewConfig firstConfig;
    firstConfig.backend = mulan::engine::GraphicsBackend::D3D11;

    mulan::view::ViewConfig secondConfig = firstConfig;
    secondConfig.vsync = !firstConfig.vsync;
    secondConfig.msaa = mulan::engine::RenderConfig::MSAALevel::x8;
    secondConfig.bufferCount = static_cast<uint8_t>(firstConfig.bufferCount + 1);
    secondConfig.clearColor[0] = 0.9f;
    secondConfig.clearColor[1] = 0.1f;

    const RenderDeviceConfig first = RenderDeviceConfig::fromView(firstConfig);
    const RenderDeviceConfig second = RenderDeviceConfig::fromView(secondConfig);

    EXPECT_TRUE(first.sharesExecutionThreadWith(second));
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
