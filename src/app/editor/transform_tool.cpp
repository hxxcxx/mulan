#include "transform_tool.h"

#include "transform_preview_builder.h"

#include <utility>

namespace mulan::app {
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
    : document_(document), context_(std::move(context)), mode_(mode), commit_mode_(commitMode) {
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

    if (isLeftRelease(input.event)) {
        if (drag_start_world_ && drag_preview_started_) {
            return commit(*point);
        }
        return EditorAction::consumeEvent();
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

EditorAction TransformTool::updatePreview(const math::Mat4& worldDelta) const {
    if (!document_) {
        return EditorAction::consumeEvent();
    }

    std::vector<EntityTransformUpdate> updates = context_.entityUpdates(worldDelta);
    if (updates.empty()) {
        return EditorAction::clearPreview();
    }

    DraftGeometry preview = TransformPreviewBuilder::build(*document_, updates);
    if (preview.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(preview));
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

}  // namespace mulan::app
