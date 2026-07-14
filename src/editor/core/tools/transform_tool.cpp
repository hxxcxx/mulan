#include "transform_tool.h"

#include <mulan/view/core/preview_layer.h>

#include <vector>
#include <utility>

namespace mulan::editor {
namespace {

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

bool isLeftRelease(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MouseRelease && event.button == engine::MouseButton::Left;
}

bool isRightPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Right;
}

bool isMouseMove(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MouseMove;
}

}  // namespace

TransformTool::TransformTool(const io::Document* document, TransformEditContext context, TransformEditMode mode,
                             TransformEditCommitMode commitMode)
    : context_(std::move(context)), mode_(mode), commit_mode_(commitMode) {
    (void) document;
}

TransformTool::TransformTool(const io::Document* document, TransformEditContext context, math::Point3 dragStartWorld,
                             TransformEditMode mode, TransformEditCommitMode commitMode)
    : TransformTool(document, std::move(context), mode, commitMode) {
    setDragStart(dragStartWorld);
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

EditorAction TransformTool::handleInput(const EditorInput& input) {
    if (isRightPress(input.event)) {
        return EditorAction::cancel();
    }

    // 生命周期事件优先于空间数据（修 P7）：release 必须能结束工具，即使 worldPoint 缺失。
    if (isLeftRelease(input.event)) {
        if (drag_start_world_ && drag_preview_started_) {
            // worldPoint 缺失时用已记录的增量回退，确保 release 总能完成。
            const auto point = input.worldPoint();
            return point ? commit(*point) : commitWithLastDelta();
        }
        return EditorAction::cancel();  // 无可提交内容：取消而非吞事件
    }

    const auto point = input.worldPoint();
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (isLeftPress(input.event)) {
        if (!drag_start_world_) {
            return setDragStart(*point);
        }
        return commit(*point);
    }

    if (isMouseMove(input.event)) {
        return update(*point);
    }

    return EditorAction::ignored();
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

EditorAction TransformTool::update(const math::Point3& worldPoint) {
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

    EditorAction action = EditorAction::commit(std::move(operation));
    action.clearPreviewOnApply().finishTool();
    return action;
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

    EditorAction action = EditorAction::commit(std::move(operation));
    action.clearPreviewOnApply().finishTool();
    return action;
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
