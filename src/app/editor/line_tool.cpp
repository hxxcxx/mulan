/**
 * @file line_tool.cpp
 * @brief LineTool 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "line_tool.h"

#include <mulan/view/preview_layer.h>

namespace mulan::app {

namespace {

constexpr double kMinimumLineLengthSq = 1.0e-12;

bool isLeftPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Left;
}

bool isRightPress(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MousePress && event.button == engine::MouseButton::Right;
}

bool isMouseMove(const engine::InputEvent& event) {
    return event.type == engine::InputEvent::Type::MouseMove;
}

bool isLineEvent(const engine::InputEvent& event) {
    return isLeftPress(event) || isRightPress(event) || isMouseMove(event);
}

}  // namespace

void LineTool::begin(ToolContext& context) {
    step_ = Step::FirstPoint;
    first_point_.reset();
    context.clearPreview();
}

ToolInputResult LineTool::handleInput(ToolContext& context, const EditorInput& input) {
    if (isRightPress(input.event)) {
        return ToolInputResult::Cancelled;
    }

    if (!isLineEvent(input.event)) {
        return ToolInputResult::Ignored;
    }

    if (!input.workPoint) {
        return ToolInputResult::Consumed;
    }

    if (isMouseMove(input.event) && step_ == Step::SecondPoint) {
        updatePreview(context, *input.workPoint);
        return ToolInputResult::Consumed;
    }

    if (isLeftPress(input.event)) {
        return acceptPoint(context, *input.workPoint);
    }

    return ToolInputResult::Consumed;
}

void LineTool::end(ToolContext& context, ToolFinishReason reason) {
    if (reason != ToolFinishReason::Finished) {
        context.clearPreview();
    }
}

ToolInputResult LineTool::acceptPoint(ToolContext& context, const math::Point3& point) {
    if (step_ == Step::FirstPoint) {
        first_point_ = point;
        step_ = Step::SecondPoint;
        updatePreview(context, point);
        return ToolInputResult::Consumed;
    }

    if (!first_point_) {
        step_ = Step::FirstPoint;
        return ToolInputResult::Consumed;
    }

    const math::Segment3 segment(*first_point_, point);
    if (segment.lengthSq() <= kMinimumLineLengthSq) {
        updatePreview(context, point);
        return ToolInputResult::Consumed;
    }

    const bool created = context.createCurve("Line", asset::CurvePrimitive::segment(segment));
    context.clearPreview();
    return created ? ToolInputResult::Finished : ToolInputResult::Cancelled;
}

void LineTool::updatePreview(ToolContext& context, const math::Point3& point) {
    if (!first_point_) {
        return;
    }

    view::PreviewBuilder builder;
    builder.addSegment(math::Segment3(*first_point_, point));
    context.setPreview(builder.takeCurves());
}

}  // namespace mulan::app
