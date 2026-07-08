#include "circle_tool.h"

#include <utility>

namespace mulan::app {
namespace {

constexpr double kMinimumRadius = 1.0e-6;

}  // namespace

EditorAction CircleTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    if (!hasAcceptedPoints()) {
        return acceptCenter(point);
    }
    return acceptRadiusPoint(input, point);
}

EditorAction CircleTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    if (acceptedPointCount() != 1) {
        return EditorAction::consumeEvent();
    }
    return updatePreview(input, point);
}

EditorAction CircleTool::acceptCenter(ToolPoint point) {
    addAcceptedPoint(std::move(point));
    return EditorAction::clearPreview();
}

EditorAction CircleTool::acceptRadiusPoint(const EditorInput& input, const ToolPoint& point) const {
    const auto circle = makeCircle(input, point.world());
    if (!circle) {
        return updatePreview(input, point);
    }

    EditorAction action =
            EditorAction::commit(DocumentOperation::createCurve("Circle", asset::CurvePrimitive::circle(*circle)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

EditorAction CircleTool::updatePreview(const EditorInput& input, const ToolPoint& point) const {
    const auto circle = makeCircle(input, point.world());
    if (!circle) {
        return EditorAction::clearPreview();
    }

    return EditorAction::setPreview(DraftGeometry::curve(asset::CurvePrimitive::circle(*circle)));
}

std::optional<math::Circle3> CircleTool::makeCircle(const EditorInput& input, const math::Point3& radiusPoint) const {
    const ToolPoint* center = firstAcceptedPoint();
    if (!center) {
        return std::nullopt;
    }

    const math::Vec3 normal = input.workPlane.plane().normal.normalizedOr(math::Vec3::unitZ());
    math::Vec3 radiusVector = radiusPoint - center->world();
    radiusVector -= normal * radiusVector.dot(normal);
    const double radius = radiusVector.length();
    if (radius <= kMinimumRadius) {
        return std::nullopt;
    }

    return math::Circle3(center->world(), radius, normal);
}

}  // namespace mulan::app
