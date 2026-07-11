#include "core/grip/grip_marker_builder.h"

#include "core/operation/control_polygon_builder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::app {
namespace {

constexpr double kControlPointMarkerPixels = 7.0;

double markerWorldSize(const EditorGrip& grip, const engine::Camera& camera) {
    const double pixels = std::max(1.0, grip.pickRadiusPixels * 0.8);
    return controlMarkerWorldSize(camera, grip.worldPosition, pixels);
}

struct ControlGripRef {
    scene::EntityId entity = scene::EntityId::invalid();
    asset::CurveElementId element = asset::CurveElementId::invalid();
    size_t vertexIndex = 0;
    math::Point3 worldPosition;
};

void addSegment(std::vector<asset::CurvePrimitive>& curves, const math::Point3& a, const math::Point3& b) {
    if (a.distanceSq(b) <= 1.0e-18) {
        return;
    }
    curves.push_back(asset::CurvePrimitive::segment(math::Segment3(a, b)));
}

void addSquare(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const ControlMarkerBasis& basis,
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

void addDiamond(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const ControlMarkerBasis& basis,
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

void addCross(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center, const ControlMarkerBasis& basis,
              double size) {
    addSegment(curves, center - basis.x * size, center + basis.x * size);
    addSegment(curves, center - basis.y * size, center + basis.y * size);
}

void addTriangle(std::vector<asset::CurvePrimitive>& curves, const math::Point3& center,
                 const ControlMarkerBasis& basis, double size) {
    const math::Point3 a = center + basis.y * size;
    const math::Point3 b = center - basis.x * size - basis.y * size;
    const math::Point3 c = center + basis.x * size - basis.y * size;
    addSegment(curves, a, b);
    addSegment(curves, b, c);
    addSegment(curves, c, a);
}

void addControlPolygonLines(std::span<const EditorGrip> grips, std::vector<asset::CurvePrimitive>& curves) {
    std::vector<ControlGripRef> controls;
    controls.reserve(grips.size());
    for (const EditorGrip& grip : grips) {
        if (grip.kind != EditorGripKind::ControlPoint || grip.action != EditorGripAction::MoveControlPoint) {
            continue;
        }
        controls.push_back(ControlGripRef{
                .entity = grip.entity,
                .element = grip.element,
                .vertexIndex = grip.vertexIndex,
                .worldPosition = grip.worldPosition,
        });
    }

    std::sort(controls.begin(), controls.end(), [](const ControlGripRef& lhs, const ControlGripRef& rhs) {
        if (lhs.entity.value != rhs.entity.value) {
            return lhs.entity.value < rhs.entity.value;
        }
        if (lhs.element.value != rhs.element.value) {
            return lhs.element.value < rhs.element.value;
        }
        return lhs.vertexIndex < rhs.vertexIndex;
    });

    for (size_t i = 0; i < controls.size();) {
        size_t j = i + 1;
        while (j < controls.size() && controls[j].entity == controls[i].entity &&
               controls[j].element == controls[i].element) {
            ++j;
        }

        if (j - i >= 2) {
            std::vector<math::Point3> points;
            points.reserve(j - i);
            for (size_t k = i; k < j; ++k) {
                points.push_back(controls[k].worldPosition);
            }
            curves.push_back(asset::CurvePrimitive::polyline(math::Polyline3(std::move(points), false)));
        }
        i = j;
    }
}

void addGripMarker(std::vector<asset::CurvePrimitive>& curves, std::vector<graphics::Mesh>& meshes,
                   const EditorGrip& grip, const engine::Camera& camera, const ControlMarkerBasis& basis,
                   double sizeScale) {
    const double size = markerWorldSize(grip, camera) * sizeScale;
    switch (grip.kind) {
    case EditorGripKind::Vertex: addSquare(curves, grip.worldPosition, basis, size); break;
    case EditorGripKind::ControlPoint:
        meshes.push_back(buildControlPointDisk(
                grip.worldPosition, basis,
                controlMarkerWorldSize(camera, grip.worldPosition, kControlPointMarkerPixels) * sizeScale));
        break;
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
    std::vector<graphics::Mesh> meshes;
    curves.reserve(grips.size() * 4);
    meshes.reserve(grips.size());
    addControlPolygonLines(grips, curves);
    const ControlMarkerBasis basis = controlMarkerBasisFromCamera(camera);
    for (const EditorGrip& grip : grips) {
        if (excludedGrip && grip.id == *excludedGrip) {
            continue;
        }
        addGripMarker(curves, meshes, grip, camera, basis, 1.0);
    }
    return DraftGeometry::geometry(std::move(curves), std::move(meshes));
}

DraftGeometry GripMarkerBuilder::buildHot(const EditorGrip& grip, const engine::Camera& camera) {
    std::vector<asset::CurvePrimitive> curves;
    std::vector<graphics::Mesh> meshes;
    const ControlMarkerBasis basis = controlMarkerBasisFromCamera(camera);
    addGripMarker(curves, meshes, grip, camera, basis, 1.25);
    return DraftGeometry::geometry(std::move(curves), std::move(meshes));
}

}  // namespace mulan::app
