/**
 * @file render_resource_outbox.h
 * @brief View 到渲染执行域的可靠持久资源批次 Outbox。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include <mulan/render/frontend/render_frame_submission.h>

#include <cstdint>

namespace mulan::view {

/// 未确认资源操作会随帧重复提交；新操作按资源身份覆盖旧操作，只有当前批次 ACK 才能清空。
class RenderResourceOutbox {
public:
    void reset();
    void invalidate();
    void merge(const engine::RenderResourcePrepareList& updates);
    void acknowledge(uint64_t batchId);
    void attachTo(engine::RenderFrameSubmission& submission) const;

private:
    void advanceBatch();

    engine::RenderResourcePrepareList pending_;
    uint64_t batch_id_ = 0;
};

}  // namespace mulan::view
