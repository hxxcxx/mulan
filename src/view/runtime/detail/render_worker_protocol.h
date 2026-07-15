/**
 * @file render_worker_protocol.h
 * @brief RenderWorker 的资源序号、帧依赖与 owner 回执协议。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 本类型不执行 GPU 工作，只维护跨线程协议状态。调用方必须在同一把队列锁下访问：
 * 资源批次可靠有序；视觉帧记录提交时已知的最后资源序号；只有该序号完成后帧才可执行。
 * 资源 ACK 仅由成功完成产生，失败只发布 Failure，绝不伪造 ACK。
 */
#pragma once

#include <mulan/core/result/error.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace mulan::view::detail {

enum class RenderWorkerEventType : uint8_t {
    ResourceBatchCompleted,
    Failure,
};

struct RenderWorkerEvent {
    RenderWorkerEventType type = RenderWorkerEventType::ResourceBatchCompleted;
    uint64_t resourceSequence = 0;
    uint64_t resourceBatchId = 0;
    Error error;
};

struct ResourceRegistration {
    uint64_t sequence = 0;
    bool newlyQueued = false;
};

class RenderWorkerProtocol {
public:
    /// 注册 builder 资源批次。同一活动批次被重复提交时复用序号，不重复排队。
    ResourceRegistration registerResourceBatch(uint64_t batchId) {
        if (batchId != 0 && batchId == active_batch_id_) {
            return ResourceRegistration{ active_batch_sequence_, false };
        }

        const uint64_t sequence = advanceSequence();
        active_batch_id_ = batchId;
        active_batch_sequence_ = sequence;
        return ResourceRegistration{ sequence, true };
    }

    /// 注册清空资源域等可靠屏障；屏障之后旧 batch 不再具备去重资格。
    uint64_t registerResourceBarrier() {
        active_batch_id_ = 0;
        active_batch_sequence_ = 0;
        return advanceSequence();
    }

    uint64_t currentDependency() const { return last_enqueued_sequence_; }

    /// latest-frame 只能在其提交时观察到的最后资源序号完成后执行。
    bool canExecuteFrame(uint64_t requiredSequence) const {
        return requiredSequence == 0 || requiredSequence == completed_sequence_;
    }

    /// 资源任务按可靠队列顺序完成。batchId==0 表示内部屏障，不向 owner 发送 ACK。
    bool completeResource(uint64_t sequence, uint64_t batchId) {
        if (failed_ || sequence == 0 || sequence != nextSequence(completed_sequence_)) {
            return false;
        }
        completed_sequence_ = sequence;
        if (batchId != 0) {
            events_.push_back(RenderWorkerEvent{
                    .type = RenderWorkerEventType::ResourceBatchCompleted,
                    .resourceSequence = sequence,
                    .resourceBatchId = batchId,
            });
        }
        return true;
    }

    /// 失败快照持久保留；事件只发布一次，且失败路径不会发布资源 ACK。
    void fail(Error error, uint64_t sequence = 0, uint64_t batchId = 0) {
        if (failed_) {
            return;
        }
        failed_ = error;
        events_.push_back(RenderWorkerEvent{
                .type = RenderWorkerEventType::Failure,
                .resourceSequence = sequence,
                .resourceBatchId = batchId,
                .error = std::move(error),
        });
    }

    std::vector<RenderWorkerEvent> drainEvents() {
        std::vector<RenderWorkerEvent> drained;
        drained.reserve(events_.size());
        while (!events_.empty()) {
            drained.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return drained;
    }

    const std::optional<Error>& failure() const { return failed_; }

    void reset() {
        last_enqueued_sequence_ = 0;
        completed_sequence_ = 0;
        active_batch_id_ = 0;
        active_batch_sequence_ = 0;
        events_.clear();
        failed_.reset();
    }

private:
    static uint64_t nextSequence(uint64_t value) {
        ++value;
        return value == 0 ? 1 : value;
    }

    uint64_t advanceSequence() {
        last_enqueued_sequence_ = nextSequence(last_enqueued_sequence_);
        return last_enqueued_sequence_;
    }

    uint64_t last_enqueued_sequence_ = 0;
    uint64_t completed_sequence_ = 0;
    uint64_t active_batch_id_ = 0;
    uint64_t active_batch_sequence_ = 0;
    std::deque<RenderWorkerEvent> events_;
    std::optional<Error> failed_;
};

}  // namespace mulan::view::detail
