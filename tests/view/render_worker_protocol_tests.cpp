/**
 * @file render_worker_protocol_tests.cpp
 * @brief RenderWorker 资源序号、帧依赖与 ACK/失败协议测试。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 纯状态机测试，不初始化 GPU、窗口或真实 RenderExecutor。
 */

#include "runtime/detail/render_worker_protocol.h"

#include <gtest/gtest.h>

using namespace mulan::view::detail;

TEST(RenderWorkerProtocolTests, RepeatedPendingBatchUsesOneReliableSequenceAndOneAck) {
    RenderWorkerProtocol protocol;

    const ResourceRegistration first = protocol.registerResourceBatch(41);
    const ResourceRegistration repeated = protocol.registerResourceBatch(41);

    EXPECT_TRUE(first.newlyQueued);
    EXPECT_FALSE(repeated.newlyQueued);
    EXPECT_EQ(repeated.sequence, first.sequence);
    ASSERT_TRUE(protocol.completeResource(first.sequence, 41));

    const auto events = protocol.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().type, RenderWorkerEventType::ResourceBatchCompleted);
    EXPECT_EQ(events.front().resourceBatchId, 41u);
}

TEST(RenderWorkerProtocolTests, FrameCannotPassNewestRequiredResourceBatch) {
    RenderWorkerProtocol protocol;
    const ResourceRegistration first = protocol.registerResourceBatch(1);
    const ResourceRegistration second = protocol.registerResourceBatch(2);
    const uint64_t frameDependency = protocol.currentDependency();

    EXPECT_EQ(frameDependency, second.sequence);
    EXPECT_FALSE(protocol.canExecuteFrame(frameDependency));
    ASSERT_TRUE(protocol.completeResource(first.sequence, 1));
    EXPECT_FALSE(protocol.canExecuteFrame(frameDependency));
    ASSERT_TRUE(protocol.completeResource(second.sequence, 2));
    EXPECT_TRUE(protocol.canExecuteFrame(frameDependency));
}

TEST(RenderWorkerProtocolTests, CompletionMustBeOrderedAndAcksPreserveThatOrder) {
    RenderWorkerProtocol protocol;
    const ResourceRegistration first = protocol.registerResourceBatch(11);
    const ResourceRegistration second = protocol.registerResourceBatch(12);

    EXPECT_FALSE(protocol.completeResource(second.sequence, 12));
    EXPECT_TRUE(protocol.drainEvents().empty());
    ASSERT_TRUE(protocol.completeResource(first.sequence, 11));
    ASSERT_TRUE(protocol.completeResource(second.sequence, 12));

    const auto events = protocol.drainEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].resourceBatchId, 11u);
    EXPECT_EQ(events[1].resourceBatchId, 12u);
}

TEST(RenderWorkerProtocolTests, ResourceBarrierInvalidatesOldBatchDeduplication) {
    RenderWorkerProtocol protocol;
    const ResourceRegistration beforeClear = protocol.registerResourceBatch(7);
    const uint64_t clearBarrier = protocol.registerResourceBarrier();
    const ResourceRegistration afterClear = protocol.registerResourceBatch(7);

    EXPECT_TRUE(beforeClear.newlyQueued);
    EXPECT_GT(clearBarrier, beforeClear.sequence);
    EXPECT_TRUE(afterClear.newlyQueued);
    EXPECT_GT(afterClear.sequence, clearBarrier);
}

TEST(RenderWorkerProtocolTests, FailureIsObservableAndNeverProducesFalseAck) {
    RenderWorkerProtocol protocol;
    const ResourceRegistration batch = protocol.registerResourceBatch(9);
    const mulan::Error failure = mulan::Error::make(mulan::ErrorCode::Internal, "resource upload failed");

    protocol.fail(failure, batch.sequence, 9);
    EXPECT_FALSE(protocol.completeResource(batch.sequence, 9));
    ASSERT_TRUE(protocol.failure().has_value());

    const auto events = protocol.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().type, RenderWorkerEventType::Failure);
    EXPECT_EQ(events.front().resourceBatchId, 9u);
    EXPECT_EQ(events.front().error.message, failure.message);
    ASSERT_TRUE(protocol.failure().has_value());
    EXPECT_EQ(protocol.failure()->message, failure.message);
}
