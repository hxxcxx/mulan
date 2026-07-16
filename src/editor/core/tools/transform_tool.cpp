#include "transform_tool.h"

#include <mulan/view/core/preview_layer.h>

#include <vector>
#include <utility>

namespace mulan::editor {

TransformTool::TransformTool(const io::Document* document, TransformEditContext context, TransformEditMode mode,
                             TransformEditCommitMode commitMode)
    : context_(std::move(context)), mode_(mode), commit_mode_(commitMode) {
    (void) document;
}

TransformTool::TransformTool(const io::Document* document, TransformEditContext context, math::Point3 dragStartWorld,
                             TransformEditMode mode, TransformEditCommitMode commitMode)
    : TransformTool(document, std::move(context), mode, commitMode) {
    setDragStart(dragStartWorld);
    commit_on_release_ = true;
}

EditorPointPolicy TransformTool::pointPolicy() const {
    EditorPointPolicy policy;
    if (drag_start_world_) {
        policy.axisAnchor = *drag_start_world_;
    }
    return policy;
}

EditorAction TransformTool::begin() {
    current_delta_.reset();
    drag_preview_started_ = false;
    return EditorAction::clearPreview();
}

EditorAction TransformTool::onAnchorPress(const EditorInput& /*input*/, const math::Point3& worldPoint) {
    // 首次 press 设定锚点；再次 press 直接提交（确认变换）。
    if (!drag_start_world_) {
        return setDragStart(worldPoint);
    }
    return commit(worldPoint);
}

EditorAction TransformTool::updateDragPreview(const EditorInput& /*input*/, const math::Point3& worldPoint) {
    if (!drag_start_world_) {
        return EditorAction::consumeEvent();
    }

    current_delta_ = worldDelta(worldPoint);
    if (!current_delta_) {
        return EditorAction::clearPreview();
    }

    drag_preview_started_ = true;
    return updatePreview(*current_delta_);
}

EditorAction TransformTool::commitAtPoint(const EditorInput& /*input*/, const math::Point3& worldPoint) {
    // 普通 Move/Copy 使用两次 press 确认。基点点击期间即使产生鼠标抖动和预览，
    // release 也不能提前提交；只有显式拖动入口采用 release 提交。
    if (commit_on_release_ && drag_start_world_ && drag_preview_started_) {
        return commit(worldPoint);
    }
    return EditorAction::consumeEvent();
}

std::optional<EditorAction> TransformTool::commitFallback(const EditorInput& /*input*/) {
    // 显式拖动入口在 worldPoint 缺失时使用最后增量完成 release 提交。
    if (commit_on_release_ && drag_start_world_ && drag_preview_started_) {
        return commitWithLastDelta();
    }
    if (drag_start_world_) {
        // release 没有可用世界点时也不能把刚设定基点的点击误判为取消。
        return EditorAction::consumeEvent();
    }
    return std::nullopt;  // 无可提交：基类将转为 cancel
}

EditorAction TransformTool::end(ToolFinishReason reason) {
    current_delta_.reset();
    drag_preview_started_ = false;
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction TransformTool::setDragStart(math::Point3 worldPoint) {
    drag_start_world_ = worldPoint;
    drag_preview_started_ = false;
    current_delta_.reset();
    context_.setAnchorWorld(worldPoint);
    return EditorAction::clearPreview();
}

EditorAction TransformTool::commit(const math::Point3& worldPoint) {
    if (!drag_start_world_) {
        return EditorAction::consumeEvent();
    }

    std::optional<math::Mat4> delta = worldDelta(worldPoint);
    if (!delta) {
        delta = current_delta_;
    }
    if (!delta) {
        return EditorAction::cancel();
    }

    std::vector<EntityTransformUpdate> updates = context_.entityUpdates(*delta);
    if (updates.empty()) {
        return EditorAction::cancel();
    }

    DocumentOperation operation = commit_mode_ == TransformEditCommitMode::Copy
                                          ? DocumentOperation::copyEntityTransforms(std::move(updates))
                                          : DocumentOperation::updateEntityTransforms(std::move(updates));

    return EditorAction::commitAndFinish(std::move(operation));
}

EditorAction TransformTool::commitWithLastDelta() const {
    // release 时 worldPoint 缺失（射线无法得到工作点）的回退路径：
    // 用最后一次 move 记录的增量提交，确保 release 总能完成变换。
    if (!current_delta_) {
        return EditorAction::cancel();
    }

    std::vector<EntityTransformUpdate> updates = context_.entityUpdates(*current_delta_);
    if (updates.empty()) {
        return EditorAction::cancel();
    }

    DocumentOperation operation = commit_mode_ == TransformEditCommitMode::Copy
                                          ? DocumentOperation::copyEntityTransforms(std::move(updates))
                                          : DocumentOperation::updateEntityTransforms(std::move(updates));

    return EditorAction::commitAndFinish(std::move(operation));
}

EditorAction TransformTool::updatePreview(const math::Mat4& worldDelta) const {
    std::vector<EntityTransformUpdate> updates = context_.entityUpdates(worldDelta);
    if (updates.empty()) {
        return EditorAction::clearPreview();
    }

    std::vector<view::PreviewReference> references;
    references.reserve(updates.size());
    for (const EntityTransformUpdate& update : updates) {
        if (!update.valid()) {
            continue;
        }
        references.push_back(view::PreviewReference{
                .entity = update.entity,
                .worldTransform = update.worldTransform,
                .overrideWorldTransform = true,
                .role = view::PreviewVisualRole::Tool,
                .visible = true,
        });
    }

    if (references.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreviewReferences(std::move(references));
}

std::optional<math::Mat4> TransformTool::worldDelta(const math::Point3& worldPoint) const {
    if (!drag_start_world_) {
        return std::nullopt;
    }

    switch (mode_) {
    case TransformEditMode::Translate: return math::Mat4::translate(worldPoint - *drag_start_world_);
    case TransformEditMode::Rotate:
    case TransformEditMode::Scale: return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace mulan::editor
