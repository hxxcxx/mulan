#include "point_drawing_tool.h"

#include <utility>

namespace mulan::editor {
namespace {

bool isPointDrawingEvent(const engine::InputEvent& event) {
    return event.isLeftPress() || event.isLeftRelease() || event.isRightPress() || event.isMouseMove();
}

}  // namespace

std::optional<ToolPoint> ToolPoint::fromInput(const EditorInput& input) {
    if (!input.point) {
        return std::nullopt;
    }

    ToolPoint pointContext;
    pointContext.point = *input.point;
    pointContext.workPlane = input.workPlane;
    pointContext.cursorRay = input.cursorRay;
    pointContext.workPoint = input.workPoint;
    pointContext.pickHit = input.pickHit;
    pointContext.geometryDependency = input.geometryDependency;
    if (!pointContext.geometryDependency && input.point->geometry) {
        pointContext.geometryDependency = input.point->geometry;
    }
    pointContext.screenX = input.screenX;
    pointContext.screenY = input.screenY;
    pointContext.hasCursor = input.hasCursor;
    pointContext.hasCursorRay = input.hasCursorRay;
    pointContext.workPlaneHit = input.workPlaneHit;
    pointContext.pickTested = input.pickTested;
    pointContext.snapResolved = input.snapResolved;
    return pointContext;
}

EditorPointPolicy PointDrawingTool::pointPolicy() const {
    EditorPointPolicy policy;
    if (const auto anchor = axisAnchor()) {
        policy.axisAnchor = *anchor;
    }
    configurePointPolicy(policy);
    return policy;
}

EditorAction PointDrawingTool::begin() {
    clearAcceptedPoints();
    resetDrawingState();
    return EditorAction::clearPreview();
}

EditorAction PointDrawingTool::handleInput(const EditorInput& input) {
    if (input.event.isRightPress()) {
        return onRightPressed(input);
    }

    if (!isPointDrawingEvent(input.event)) {
        return EditorAction::ignored();
    }

    const auto point = ToolPoint::fromInput(input);
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (input.event.isMouseMove()) {
        return onPointMoved(input, *point);
    }

    if (input.event.isLeftPress()) {
        return onPointPressed(input, *point);
    }

    if (input.event.isLeftRelease()) {
        return EditorAction::consumeEvent();
    }

    return EditorAction::consumeEvent();
}

EditorAction PointDrawingTool::end(ToolFinishReason reason) {
    clearAcceptedPoints();
    resetDrawingState();
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

const ToolPoint* PointDrawingTool::firstAcceptedPoint() const {
    if (accepted_points_.empty()) {
        return nullptr;
    }
    return &accepted_points_.front();
}

const ToolPoint* PointDrawingTool::lastAcceptedPoint() const {
    if (accepted_points_.empty()) {
        return nullptr;
    }
    return &accepted_points_.back();
}

const ToolPoint& PointDrawingTool::addAcceptedPoint(ToolPoint point) {
    accepted_points_.push_back(std::move(point));
    return accepted_points_.back();
}

void PointDrawingTool::clearAcceptedPoints() {
    accepted_points_.clear();
}

std::vector<math::Point3> PointDrawingTool::acceptedWorldPoints() const {
    std::vector<math::Point3> points;
    points.reserve(accepted_points_.size());
    for (const ToolPoint& point : accepted_points_) {
        points.push_back(point.world());
    }
    return points;
}

EditorAction PointDrawingTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    (void) input;
    (void) point;
    return EditorAction::consumeEvent();
}

EditorAction PointDrawingTool::onRightPressed(const EditorInput& input) {
    (void) input;
    return EditorAction::cancel();
}

std::optional<math::Point3> PointDrawingTool::axisAnchor() const {
    const ToolPoint* point = lastAcceptedPoint();
    if (!point) {
        return std::nullopt;
    }
    return point->world();
}

void PointDrawingTool::configurePointPolicy(EditorPointPolicy& policy) const {
    (void) policy;
}

void PointDrawingTool::resetDrawingState() {
}

}  // namespace mulan::editor
