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
    state_ = State::AwaitingStart;
    first_point_.reset();
    current_point_.reset();
    return EditorAction::clearPreview();
}

EditorPointPolicy LineTool::pointPolicy() const {
    EditorPointPolicy policy;
    if (state_ == State::RubberBand && first_point_) {
        policy.axisAnchor = first_point_;
    }
    return policy;
}

EditorAction LineTool::handleInput(const EditorInput& input) {
    if (isRightPress(input.event)) {
        return EditorAction::cancel();
    }

    if (!isLineEvent(input.event)) {
        return EditorAction::ignored();
    }

    const auto point = input.worldPoint();
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (isMouseMove(input.event) && state_ == State::RubberBand) {
        return updateRubberBand(*point);
    }

    if (isLeftPress(input.event)) {
        if (state_ == State::AwaitingStart) {
            return acceptStartPoint(*point);
        }
        return acceptEndPoint(*point);
    }

    if (isLeftRelease(input.event)) {
        return EditorAction::consumeEvent();
    }

    return EditorAction::consumeEvent();
}

EditorAction LineTool::end(ToolFinishReason reason) {
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction LineTool::acceptStartPoint(const math::Point3& point) {
    first_point_ = point;
    current_point_ = point;
    state_ = State::RubberBand;
    return EditorAction::clearPreview();
}

EditorAction LineTool::acceptEndPoint(const math::Point3& point) {
    if (!first_point_) {
        state_ = State::AwaitingStart;
        current_point_.reset();
        return EditorAction::clearPreview();
    }

    current_point_ = point;
    const math::Segment3 segment(*first_point_, point);
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        return updateRubberBand(point);
    }

    EditorAction action =
            EditorAction::commit(DocumentOperation::createCurve("Line", asset::CurvePrimitive::segment(segment)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction LineTool::updateRubberBand(const math::Point3& point) {
    if (!first_point_) {
        return EditorAction::ignored();
    }

    current_point_ = point;
    const math::Segment3 segment(*first_point_, point);
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        return EditorAction::clearPreview();
    }

    return EditorAction::setPreview(DraftGeometry::segment(segment));
}

}  // namespace mulan::app
