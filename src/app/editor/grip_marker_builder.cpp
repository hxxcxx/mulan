#include "grip_marker_builder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::app {
namespace {

struct GripMarkerBasis {
    math::Vec3 x;
    math::Vec3 y;
};

GripMarkerBasis markerBasis(const engine::Camera& camera) {
    return GripMarkerBasis{
        .x = camera.right().normalizedOr(math::Vec3::unitX()),
        .y = camera.up().normalizedOr(math::Vec3::unitY()),
    };
}

double markerWorldSize(const EditorGrip& grip, const engine::Camera& camera) {
    const double pixels = std::max(1.0, grip.pickRadiusPixels * 0.8);
    const double viewportHeight = static_cast<double>(std::max(1, camera.height()));
    if (camera.isOrthographic()) {
        return pixels * (2.0 * camera.orthoSize()) / viewportHeight;
    }

    const double depth =
            std::max(camera.nearPlane(), (grip.worldPosition.asVec() - camera.eyePosition()).dot(camera.forward()));
    const double viewHeightAtPoint = 2.0 * depth * std::tan(camera.fieldOfView() * 0.5);
    return pixels * viewHeightAtPoint / viewportHeight;
}

void addSegment(std::vector<asset::CurvePrimitive>& curves, const math::Point3& a, const math::Point3& b) {
    if (a.distanceSq(b) <= 1.0e-18) {
        return;
    }
    curves.push_back(asset::CurvePrimitive::segment(math::Segment3(a, b)));
}

void addSquare(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const GripMarkerBasis& basis,
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

void addDiamond(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const GripMarkerBasis& basis,
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

void addCross(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const GripMarkerBasis& basis,
              double size) {
    addSegment(curves, center - basis.x * size, center + basis.x * size);
    addSegment(curves, center - basis.y * size, center + basis.y * size);
}

void addTriangle(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const GripMarkerBasis& basis,
                 double size) {
    const math::Point3 a = center + basis.y * size;
    const math::Point3 b = center - basis.x * size - basis.y * size;
    const math::Point3 c = center + basis.x * size - basis.y * size;
    addSegment(curves, a, b);
    addSegment(curves, b, c);
    addSegment(curves, c, a);
}

void addGripMarker(std::vector<asset::CurvePrimitive>& curves, const EditorGrip& grip, const engine::Camera& camera,
                   const GripMarkerBasis& basis, double sizeScale) {
    const double size = markerWorldSize(grip, camera) * sizeScale;
    switch (grip.kind) {
    case EditorGripKind::Vertex: addSquare(curves, grip.worldPosition, basis, size); break;
    case EditorGripKind::Midpoint: addDiamond(curves, grip.worldPosition, basis, size); break;
    case EditorGripKind::Center:
        addCross(curves, grip.worldPosition, basis, size);
        addSquare(curves, grip.worldPosition, basis, size * 0.55);
        break;
    case EditorGripKind::Radius: addTriangle(curves, grip.worldPosition, basis, size); break;
    }
}

}  // namespace

DraftGeometry GripMarkerBuilder::build(std::span<const EditorGrip> grips, const engine::Camera& camera,
                                       std::optional<EditorGripId> excludedGrip) {
    std::vector<asset::CurvePrimitive> curves;
    curves.reserve(grips.size() * 4);
    const GripMarkerBasis basis = markerBasis(camera);
    for (const EditorGrip& grip : grips) {
        if (excludedGrip && grip.id == *excludedGrip) {
            continue;
        }
        addGripMarker(curves, grip, camera, basis, 1.0);
    }
    return DraftGeometry::curves(std::move(curves));
}

DraftGeometry GripMarkerBuilder::buildHot(const EditorGrip& grip, const engine::Camera& camera) {
    std::vector<asset::CurvePrimitive> curves;
    const GripMarkerBasis basis = markerBasis(camera);
    addGripMarker(curves, grip, camera, basis, 1.25);
    return DraftGeometry::curves(std::move(curves));
}

}  // namespace mulan::app
