/**
 * @file render_channel_state.h
 * @brief RenderChannel 的资源批次回执与失败快照状态。
 * @author hxxcxx
 * @date 2026-07-17
 *
 * 本类型不执行 GPU 工作，只维护跨线程边界上不能丢失的最小状态。资源任务的
 * 执行顺序由 RenderThread 的单消费者和每通道 FIFO 控制队列保证；这里仅负责
 * 对重复批次去重、发布最新成功 ACK，并持久保存首次失败。
 */
#pragma once

#include <mulan/core/result/error.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace mulan::view::detail {

class RenderChannelState {
public:
    /// 同一活动批次会由 Builder 重复提交；只有首次观察到它时才需要排队上传。
    bool beginResourceBatch(uint64_t batchId) {
        if (batchId == 0 || batchId == active_resource_batch_id_) {
            return false;
        }
        active_resource_batch_id_ = batchId;
        return true;
    }

    /// 清空当前资源客户端后，旧批次不再具备去重资格。
    void invalidateResourceBatch() { active_resource_batch_id_ = 0; }

    /// 可靠队列已经保证完成顺序；owner 只需要最后完成的非零批次。
    void completeResourceBatch(uint64_t batchId) {
        if (!failure_ && batchId != 0) {
            completed_resource_batch_id_ = batchId;
        }
    }

    std::optional<uint64_t> takeCompletedResourceBatch() {
        return std::exchange(completed_resource_batch_id_, std::nullopt);
    }

    /// 首次失败是根因；失败后不再发布资源 ACK。
    void fail(Error error) {
        if (!failure_) {
            failure_ = std::move(error);
            completed_resource_batch_id_.reset();
        }
    }

    const std::optional<Error>& failure() const { return failure_; }

private:
    uint64_t active_resource_batch_id_ = 0;
    std::optional<uint64_t> completed_resource_batch_id_;
    std::optional<Error> failure_;
};

}  // namespace mulan::view::detail
