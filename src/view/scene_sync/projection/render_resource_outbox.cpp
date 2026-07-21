/**
 * @file render_resource_outbox.cpp
 * @brief 可靠持久资源批次 Outbox 实现。
 * @author hxxcxx
 * @date 2026-07-21
 */

#include "render_resource_outbox.h"

namespace mulan::view {

void RenderResourceOutbox::reset() {
    pending_.clear();
    batch_id_ = 0;
}

void RenderResourceOutbox::invalidate() {
    pending_.clear();
    advanceBatch();
}

void RenderResourceOutbox::merge(const engine::RenderResourcePrepareList& updates) {
    if (updates.empty()) {
        return;
    }
    pending_.merge(updates);
    advanceBatch();
}

void RenderResourceOutbox::acknowledge(uint64_t batchId) {
    if (batchId != 0 && batchId == batch_id_) {
        pending_.clear();
    }
}

void RenderResourceOutbox::attachTo(engine::RenderFrameSubmission& submission) const {
    submission.prepare = pending_;
    submission.resourceBatchId = pending_.empty() ? 0 : batch_id_;
}

void RenderResourceOutbox::advanceBatch() {
    if (++batch_id_ == 0) {
        batch_id_ = 1;
    }
}

}  // namespace mulan::view
