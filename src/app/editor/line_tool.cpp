/**
 * @file line_tool.cpp
 * @brief LineTool 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "line_tool.h"

namespace mulan::app {

namespace {

constexpr double kMinimumLineLengthSq = 1.0e-12;

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

bool isLineEvent(const engine::InputEvent& event) {
    return isLeftPress(event) || isLeftRelease(event) || isRightPress(event) || isMouseMove(event);
}

}  // namespace

EditorAction LineTool::begin() {
    step_ = Step::FirstPoint;
    first_point_.reset();
    return EditorAction::clearPreview();
}

EditorAction LineTool::handleInput(const EditorInput& input) {
    if (isRightPress(input.event)) {
        return EditorAction::cancel();
    }

    if (!isLineEvent(input.event)) {
        return EditorAction::ignored();
    }

    if (!input.workPoint) {
        return EditorAction::consumeEvent();
    }

    if (isMouseMove(input.event) && step_ == Step::SecondPoint) {
        return updatePreview(*input.workPoint);
    }

    if (isLeftRelease(input.event)) {
        return acceptPoint(*input.workPoint);
    }

    return EditorAction::consumeEvent();
}

EditorAction LineTool::end(ToolFinishReason reason) {
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction LineTool::acceptPoint(const math::Point3& point) {
    if (step_ == Step::FirstPoint) {
        first_point_ = point;
        step_ = Step::SecondPoint;
        return EditorAction::consumeEvent();
    }

    if (!first_point_) {
        step_ = Step::FirstPoint;
        return EditorAction::consumeEvent();
    }

    const math::Segment3 segment(*first_point_, point);
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        return updatePreview(point);
    }

    EditorAction action =
            EditorAction::commit(DocumentOperation::createCurve("Line", asset::CurvePrimitive::segment(segment)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction LineTool::updatePreview(const math::Point3& point) const {
    if (!first_point_) {
        return EditorAction::ignored();
    }

    return EditorAction::setPreview(DraftGeometry::segment(math::Segment3(*first_point_, point)));
}

}  // namespace mulan::app
