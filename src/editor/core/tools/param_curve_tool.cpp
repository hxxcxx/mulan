#include "param_curve_tool.h"

#include "../operation/control_polygon_builder.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace mulan::editor {
namespace {

constexpr double kMinimumControlPointDistanceSq = 1.0e-12;
constexpr double kControlPointMarkerPixels = 7.0;
constexpr double kFallbackControlPointMarkerRadius = 0.05;

int defaultSplineDegree(size_t pointCount) {
    if (pointCount < 2) {
        return 1;
    }
    return std::min(3, static_cast<int>(pointCount) - 1);
}

ControlMarkerBasis basisForPreview(const std::vector<ToolPoint>& acceptedPoints,
                                   const std::optional<ToolPoint>& cursor) {
    if (cursor) {
        return controlMarkerBasisFromNormal(cursor->workPlane.plane().normal);
    }
    if (!acceptedPoints.empty()) {
        return controlMarkerBasisFromNormal(acceptedPoints.back().workPlane.plane().normal);
    }
    return controlMarkerBasisFromNormal(math::Vec3::unitZ());
}

}  // namespace

std::string_view ParametricCurveTool::id() const {
    switch (kind_) {
    case ParametricCurveToolKind::Bezier: return "draw.bezier";
    case ParametricCurveToolKind::BSpline: return "draw.bspline";
    case ParametricCurveToolKind::NURBS: return "draw.nurbs";
    }
    return "draw.bezier";
}

EditorAction ParametricCurveTool::onPointPressed(const EditorInput& input, const ToolPoint& point) {
    return acceptPoint(input, point);
}

EditorAction ParametricCurveTool::onPointMoved(const EditorInput& input, const ToolPoint& point) {
    if (!hasAcceptedPoints()) {
        return EditorAction::consumeEvent();
    }
    return updatePreview(input, point);
}

EditorAction ParametricCurveTool::onRightPressed(const EditorInput& input) {
    (void) input;
    return finishCurve();
}

EditorAction ParametricCurveTool::acceptPoint(const EditorInput& input, ToolPoint point) {
    const ToolPoint* last = lastAcceptedPoint();
    if (last && last->world().distanceSq(point.world()) <= kMinimumControlPointDistanceSq) {
        return updatePreview(input, point);
    }

    addAcceptedPoint(std::move(point));
    DraftGeometry geometry = previewGeometry(&input, std::nullopt);
    if (geometry.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(geometry));
}

EditorAction ParametricCurveTool::updatePreview(const EditorInput& input, const ToolPoint& point) const {
    DraftGeometry geometry = previewGeometry(&input, point);
    if (geometry.empty()) {
        return EditorAction::clearPreview();
    }
    return EditorAction::setPreview(std::move(geometry));
}

EditorAction ParametricCurveTool::finishCurve() const {
    std::optional<asset::CurvePrimitive> primitive = makePrimitive(acceptedWorldPoints());
    if (!primitive) {
        return EditorAction::cancel();
    }

    EditorAction action = EditorAction::commit(DocumentOperation::createCurve(curveName(), std::move(*primitive)));
    action.clearPreviewOnApply().finishTool();
    return action;
}

DraftGeometry ParametricCurveTool::previewGeometry(const EditorInput* input,
                                                   const std::optional<ToolPoint>& cursor) const {
    std::vector<math::Point3> points = acceptedWorldPoints();
    if (cursor && (points.empty() || points.back().distanceSq(cursor->world()) > kMinimumControlPointDistanceSq)) {
        points.push_back(cursor->world());
    }

    std::vector<asset::CurvePrimitive> curves;
    std::optional<asset::CurvePrimitive> primitive = makePrimitive(points);
    if (primitive) {
        curves.push_back(std::move(*primitive));
    }

    const ControlMarkerBasis basis = basisForPreview(acceptedPoints(), cursor);
    DraftGeometry controlGeometry =
            input && input->snapQuery.camera
                    ? buildControlPolygonGeometry(points, basis, *input->snapQuery.camera, kControlPointMarkerPixels)
                    : buildControlPolygonGeometry(points, basis, kFallbackControlPointMarkerRadius);
    std::vector<asset::CurvePrimitive> controlCurves = controlGeometry.takeCurves();
    std::vector<graphics::Mesh> controlMeshes = controlGeometry.takeMeshes();
    curves.insert(curves.end(), std::make_move_iterator(controlCurves.begin()),
                  std::make_move_iterator(controlCurves.end()));
    return DraftGeometry::geometry(std::move(curves), std::move(controlMeshes));
}

std::optional<asset::CurvePrimitive> ParametricCurveTool::makePrimitive(std::vector<math::Point3> points) const {
    if (points.size() < 2) {
        return std::nullopt;
    }

    switch (kind_) {
    case ParametricCurveToolKind::Bezier: return asset::CurvePrimitive::bezier(math::BezierCurve3d(std::move(points)));
    case ParametricCurveToolKind::BSpline: {
        const int degree = defaultSplineDegree(points.size());
        const int pointCount = static_cast<int>(points.size());
        return asset::CurvePrimitive::bspline(
                math::BSplineCurve3d(degree, std::move(points), math::uniformKnotVector(degree, pointCount)));
    }
    case ParametricCurveToolKind::NURBS: {
        const int degree = defaultSplineDegree(points.size());
        const int pointCount = static_cast<int>(points.size());
        return asset::CurvePrimitive::nurbs(
                math::NURBSCurve3d(degree, std::move(points), std::vector<double>(static_cast<size_t>(pointCount), 1.0),
                                   math::uniformKnotVector(degree, pointCount)));
    }
    }
    return std::nullopt;
}

std::string ParametricCurveTool::curveName() const {
    switch (kind_) {
    case ParametricCurveToolKind::Bezier: return "Bezier";
    case ParametricCurveToolKind::BSpline: return "B-Spline";
    case ParametricCurveToolKind::NURBS: return "NURBS";
    }
    return "Curve";
}

}  // namespace mulan::editor
