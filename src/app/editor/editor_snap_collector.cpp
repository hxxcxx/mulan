#include "editor_snap_collector.h"

#include <algorithm>
#include <cmath>

namespace mulan::app {
namespace {

constexpr double kWorkPlanePriority = 0.0;
constexpr double kGridPriority = 0.5;
constexpr double kFacePointPriority = 1.5;
constexpr double kEdgeNearestPriority = 2.0;
constexpr double kAxisPriority = 2.75;
constexpr double kMidpointPriority = 3.0;
constexpr double kEndpointPriority = 4.0;

struct PlaneBasis {
    math::Point3 origin;
    math::Vec3 x;
    math::Vec3 y;
};

PlaneBasis makePlaneBasis(const engine::WorkPlane& workPlane) {
    const math::Plane3& plane = workPlane.plane();
    const math::Vec3 normal = plane.normal.normalizedOr(math::Vec3::unitZ());
    const math::Vec3 seed = std::abs(normal.z) < 0.9 ? math::Vec3::unitZ() : math::Vec3::unitY();
    const math::Vec3 x = seed.cross(normal).normalizedOr(math::Vec3::unitX());
    const math::Vec3 y = normal.cross(x).normalizedOr(math::Vec3::unitY());
    return PlaneBasis{
        .origin = math::Point3(normal * plane.d),
        .x = x,
        .y = y,
    };
}

EditorGeometryDependency geometryDependencyFromHit(const EditorPickHit& hit, EditorPickHitKind kind, double parameter) {
    return EditorGeometryDependency{
        .entity = hit.entity,
        .pickId = hit.pickId,
        .hitKind = kind,
        .distance = hit.distance,
        .sourceDrawableIndex = hit.sourceDrawableIndex,
        .primitiveIndex = hit.primitiveIndex,
        .hasPrimitiveIndex = hit.hasPrimitiveIndex,
        .parameter = parameter,
        .toleranceWorld = hit.toleranceWorld,
    };
}

double snapTolerance(const EditorPickHit& hit, const EditorSnapSettings& settings) {
    if (settings.snapToleranceWorld > 0.0) {
        return settings.snapToleranceWorld;
    }
    return std::max(0.0, hit.toleranceWorld);
}

void addWorkPlaneCandidate(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    if (!input.workPoint || !input.pointPolicy.allowWorkPlane) {
        return;
    }

    out.push_back(EditorSnapCandidate{
            .world = *input.workPoint,
            .kind = EditorSnapKind::WorkPlane,
            .dependency = EditorPointDependencyKind::WorkPlane,
            .priority = kWorkPlanePriority,
    });
}

void addGridCandidate(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    if (!input.workPoint || !input.pointPolicy.allowWorkPlane || !input.snapSettings.enabled ||
        !input.snapSettings.enableGridSnap || input.snapSettings.gridSpacing <= 0.0) {
        return;
    }

    const PlaneBasis basis = makePlaneBasis(input.workPlane);
    const math::Vec3 delta = *input.workPoint - basis.origin;
    const double spacing = input.snapSettings.gridSpacing;
    const double gx = std::round(delta.dot(basis.x) / spacing) * spacing;
    const double gy = std::round(delta.dot(basis.y) / spacing) * spacing;
    const math::Point3 gridPoint = basis.origin + basis.x * gx + basis.y * gy;
    out.push_back(EditorSnapCandidate{
            .world = gridPoint,
            .kind = EditorSnapKind::Grid,
            .dependency = EditorPointDependencyKind::Grid,
            .priority = kGridPriority,
            .distance = input.workPoint->distance(gridPoint),
    });
}

void addAxisCandidate(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    if (!input.workPoint || !input.pointPolicy.allowAxisConstraint || !input.pointPolicy.axisAnchor ||
        !input.snapSettings.enabled || !input.snapSettings.enableAxisConstraint ||
        !input.event.hasModifier(engine::KeyModifier::Shift)) {
        return;
    }

    const PlaneBasis basis = makePlaneBasis(input.workPlane);
    const math::Point3 anchor = *input.pointPolicy.axisAnchor;
    const math::Vec3 delta = *input.workPoint - anchor;

    const math::Point3 axisX = anchor + basis.x * delta.dot(basis.x);
    const math::Point3 axisY = anchor + basis.y * delta.dot(basis.y);
    const double distX = input.workPoint->distance(axisX);
    const double distY = input.workPoint->distance(axisY);
    const math::Point3 point = distX <= distY ? axisX : axisY;

    out.push_back(EditorSnapCandidate{
            .world = point,
            .kind = EditorSnapKind::Axis,
            .dependency = EditorPointDependencyKind::Axis,
            .priority = kAxisPriority,
            .distance = std::min(distX, distY),
    });
}

void addEdgeCandidates(const EditorPickHit& hit, const EditorSnapSettings& settings,
                       std::vector<EditorSnapCandidate>& out) {
    if (!hit.hasWorldPoint || !hit.hasEdgeSegment) {
        return;
    }

    const double tolerance = snapTolerance(hit, settings);
    const math::Point3 midpoint = math::lerp(hit.edgeStart, hit.edgeEnd, 0.5);

    if (settings.enableEndpointSnap && tolerance > 0.0) {
        const double startDistance = hit.worldPoint.distance(hit.edgeStart);
        if (startDistance <= tolerance) {
            out.push_back(EditorSnapCandidate{
                    .world = hit.edgeStart,
                    .kind = EditorSnapKind::Vertex,
                    .dependency = EditorPointDependencyKind::Geometry,
                    .geometry = geometryDependencyFromHit(hit, EditorPickHitKind::Vertex, 0.0),
                    .priority = kEndpointPriority,
                    .distance = startDistance,
            });
        }

        const double endDistance = hit.worldPoint.distance(hit.edgeEnd);
        if (endDistance <= tolerance) {
            out.push_back(EditorSnapCandidate{
                    .world = hit.edgeEnd,
                    .kind = EditorSnapKind::Vertex,
                    .dependency = EditorPointDependencyKind::Geometry,
                    .geometry = geometryDependencyFromHit(hit, EditorPickHitKind::Vertex, 1.0),
                    .priority = kEndpointPriority,
                    .distance = endDistance,
            });
        }
    }

    if (settings.enableMidpointSnap && tolerance > 0.0) {
        const double midpointDistance = hit.worldPoint.distance(midpoint);
        if (midpointDistance <= tolerance) {
            out.push_back(EditorSnapCandidate{
                    .world = midpoint,
                    .kind = EditorSnapKind::Midpoint,
                    .dependency = EditorPointDependencyKind::Geometry,
                    .geometry = geometryDependencyFromHit(hit, EditorPickHitKind::Edge, 0.5),
                    .priority = kMidpointPriority,
                    .distance = midpointDistance,
            });
        }
    }

    if (settings.enableEdgeNearestSnap) {
        out.push_back(EditorSnapCandidate{
                .world = hit.worldPoint,
                .kind = EditorSnapKind::Edge,
                .dependency = EditorPointDependencyKind::Geometry,
                .geometry = geometryDependencyFromHit(hit, EditorPickHitKind::Edge, hit.parameter),
                .priority = kEdgeNearestPriority,
        });
    }
}

void addGeometryCandidates(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    if (!input.snapSettings.enabled || !input.snapSettings.enableGeometrySnap || !input.pointPolicy.allowGeometry ||
        !input.pickHit || !input.pickHit->valid()) {
        return;
    }

    const EditorPickHit& hit = *input.pickHit;
    if (hit.kind == EditorPickHitKind::Edge) {
        addEdgeCandidates(hit, input.snapSettings, out);
        return;
    }

    if (hit.kind == EditorPickHitKind::Face && input.snapSettings.enableFacePointSnap && hit.hasWorldPoint) {
        out.push_back(EditorSnapCandidate{
                .world = hit.worldPoint,
                .kind = EditorSnapKind::Face,
                .dependency = EditorPointDependencyKind::Geometry,
                .geometry = geometryDependencyFromHit(hit, EditorPickHitKind::Face, hit.parameter),
                .priority = kFacePointPriority,
        });
    }
}

}  // namespace

void EditorSnapCollector::collect(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    addWorkPlaneCandidate(input, out);
    addGridCandidate(input, out);
    addAxisCandidate(input, out);
    addGeometryCandidates(input, out);
}

}  // namespace mulan::app
