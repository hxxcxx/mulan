#include "transform_tool.h"

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

TransformTool::TransformTool(TransformEditContext context, math::Point3 dragStartWorld, TransformEditMode mode)
    : context_(std::move(context)), mode_(mode), drag_start_world_(dragStartWorld) {
    context_.setAnchorWorld(dragStartWorld);
}

EditorPointPolicy TransformTool::pointPolicy() const {
    EditorPointPolicy policy;
    policy.axisAnchor = drag_start_world_;
    return policy;
}

EditorAction TransformTool::begin() {
    current_delta_.reset();
    return EditorAction::clearPreview();
}

EditorAction TransformTool::handleInput(const EditorInput& input) {
    if (isRightPress(input.event)) {
        return EditorAction::cancel();
    }

    if (isLeftPress(input.event)) {
        return EditorAction::consumeEvent();
    }

    const auto point = input.worldPoint();
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (isMouseMove(input.event)) {
        return update(*point);
    }

    if (isLeftRelease(input.event)) {
        return commit(*point);
    }

    return EditorAction::ignored();
}

EditorAction TransformTool::end(ToolFinishReason reason) {
    current_delta_.reset();
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction TransformTool::update(const math::Point3& worldPoint) {
    current_delta_ = worldDelta(worldPoint);
    return EditorAction::consumeEvent();
}

EditorAction TransformTool::commit(const math::Point3& worldPoint) {
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

    EditorAction action = EditorAction::commit(DocumentOperation::updateEntityTransforms(std::move(updates)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

std::optional<math::Mat4> TransformTool::worldDelta(const math::Point3& worldPoint) const {
    switch (mode_) {
    case TransformEditMode::Translate: return math::Mat4::translate(worldPoint - drag_start_world_);
    case TransformEditMode::Rotate:
    case TransformEditMode::Scale: return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace mulan::app
