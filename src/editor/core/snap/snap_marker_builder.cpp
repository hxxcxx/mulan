#include "snap_marker_builder.h"

#include <mulan/render/camera/camera.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace mulan::editor {
namespace {

struct MarkerBasis {
    math::Vec3 x;
    math::Vec3 y;
};

MarkerBasis markerBasis(const engine::WorkPlane& workPlane) {
    const math::Vec3 normal = workPlane.plane().normal.normalizedOr(math::Vec3::unitZ());
    const math::Vec3 seed = std::abs(normal.z) < 0.9 ? math::Vec3::unitZ() : math::Vec3::unitY();
    const math::Vec3 x = seed.cross(normal).normalizedOr(math::Vec3::unitX());
    const math::Vec3 y = normal.cross(x).normalizedOr(math::Vec3::unitY());
    return MarkerBasis{ .x = x, .y = y };
}

std::optional<double> markerSizeFromCamera(const EditorInput& input) {
    if (!input.point || !input.snapQuery.camera || input.snapQuery.camera->height() <= 0) {
        return std::nullopt;
    }

    const engine::Camera& camera = *input.snapQuery.camera;
    const double pixels = std::max(1.0, input.snapQuery.snapSettings.markerSizePixels);
    const double viewportHeight = static_cast<double>(std::max(1, camera.height()));
    if (camera.isOrthographic()) {
        return pixels * (2.0 * camera.orthoSize()) / viewportHeight;
    }

    const double depth =
            std::max(camera.nearPlane(), (input.point->world.asVec() - camera.eyePosition()).dot(camera.forward()));
    const double viewHeightAtPoint = 2.0 * depth * std::tan(camera.fieldOfView() * 0.5);
    return pixels * viewHeightAtPoint / viewportHeight;
}

double markerSize(const EditorInput& input) {
    if (const auto cameraSize = markerSizeFromCamera(input)) {
        return std::max(1.0e-6, *cameraSize);
    }
    if (input.geometryDependency && input.geometryDependency->toleranceWorld > 0.0) {
        return std::max(1.0e-6, input.geometryDependency->toleranceWorld * 0.75);
    }
    if (input.pickHit && input.pickHit->toleranceWorld > 0.0) {
        return std::max(1.0e-6, input.pickHit->toleranceWorld * 0.75);
    }
    return 0.1;
}

void addSegment(std::vector<asset::CurvePrimitive>& curves, const math::Point3& a, const math::Point3& b) {
    if (a.distanceSq(b) <= 1.0e-18) {
        return;
    }
    curves.push_back(asset::CurvePrimitive::segment(math::Segment3(a, b)));
}

void addCross(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const MarkerBasis& basis,
              double size) {
    addSegment(curves, center - basis.x * size, center + basis.x * size);
    addSegment(curves, center - basis.y * size, center + basis.y * size);
}

void addSquare(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const MarkerBasis& basis,
               double size) {
    const math::Point3 a = center + basis.x * size + basis.y * size;
    const math::Point3 b = center - basis.x * size + basis.y * size;
    const math::Point3 c = center - basis.x * size - basis.y * size;
    const math::Point3 d = center + basis.x * size - basis.y * size;
    addSegment(curves, a, b);
    addSegment(curves, b, c);
    addSegment(curves, c, d);
    addSegment(curves, d, a);
}

void addDiamond(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const MarkerBasis& basis,
                double size) {
    const math::Point3 a = center + basis.y * size;
    const math::Point3 b = center - basis.x * size;
    const math::Point3 c = center - basis.y * size;
    const math::Point3 d = center + basis.x * size;
    addSegment(curves, a, b);
    addSegment(curves, b, c);
    addSegment(curves, c, d);
    addSegment(curves, d, a);
}

void addTriangle(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const MarkerBasis& basis,
                 double size) {
    const math::Point3 a = center + basis.y * size;
    const math::Point3 b = center - basis.x * size - basis.y * size;
    const math::Point3 c = center + basis.x * size - basis.y * size;
    addSegment(curves, a, b);
    addSegment(curves, b, c);
    addSegment(curves, c, a);
}

void addMarkerForPoint(std::vector<asset::CurvePrimitive>& curves, const EditorInput& input) {
    if (!input.point) {
        return;
    }

    const MarkerBasis basis = markerBasis(input.workPlane);
    const double size = markerSize(input);
    const math::Point3 center = input.point->world;

    switch (input.point->snapKind) {
    case EditorSnapKind::Vertex: addSquare(curves, center, basis, size); break;
    case EditorSnapKind::Midpoint: addTriangle(curves, center, basis, size); break;
    case EditorSnapKind::Center:
        addCross(curves, center, basis, size);
        addSquare(curves, center, basis, size * 0.55);
        break;
    case EditorSnapKind::Tangent:
        addTriangle(curves, center, basis, size * 0.9);
        addCross(curves, center, basis, size * 0.45);
        break;
    case EditorSnapKind::Edge: addDiamond(curves, center, basis, size); break;
    case EditorSnapKind::Face: addCross(curves, center, basis, size); break;
    case EditorSnapKind::Grid: addCross(curves, center, basis, size * 0.6); break;
    case EditorSnapKind::Axis: addDiamond(curves, center, basis, size * 0.8); break;
    case EditorSnapKind::Curve: addDiamond(curves, center, basis, size * 0.7); break;
    case EditorSnapKind::WorkPlane:
    case EditorSnapKind::Depth:
    case EditorSnapKind::None: break;
    }
}

void addAxisMarker(std::vector<asset::CurvePrimitive>& curves, const EditorInput& input) {
    if (!input.point || input.point->snapKind != EditorSnapKind::Axis || !input.axisAnchor) {
        return;
    }
    addSegment(curves, *input.axisAnchor, input.point->world);
}

}  // namespace

DraftGeometry SnapMarkerBuilder::build(const EditorInput& input) {
    std::vector<asset::CurvePrimitive> curves;
    addAxisMarker(curves, input);
    addMarkerForPoint(curves, input);
    return DraftGeometry::curves(std::move(curves));
}

}  // namespace mulan::editor
