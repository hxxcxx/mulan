#include "editor_snap_collector.h"

#include "../selection/editor_scene_snap_provider.h"

#include <algorithm>
#include <cmath>

namespace mulan::editor {
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
    const EditorSnapQuery& query = input.query;
    if (!query.workPoint || !query.pointPolicy.allowWorkPlane) {
        return;
    }

    out.push_back(EditorSnapCandidate{
            .world = *query.workPoint,
            .kind = EditorSnapKind::WorkPlane,
            .dependency = EditorPointDependencyKind::WorkPlane,
            .priority = kWorkPlanePriority,
    });
}

void addGridCandidate(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    const EditorSnapQuery& query = input.query;
    if (!query.workPoint || !query.pointPolicy.allowWorkPlane || !query.snapSettings.enabled ||
        !query.snapSettings.enableGridSnap || query.snapSettings.gridSpacing <= 0.0) {
        return;
    }

    const PlaneBasis basis = makePlaneBasis(query.workPlane);
    const math::Vec3 delta = *query.workPoint - basis.origin;
    const double spacing = query.snapSettings.gridSpacing;
    const double gx = std::round(delta.dot(basis.x) / spacing) * spacing;
    const double gy = std::round(delta.dot(basis.y) / spacing) * spacing;
    const math::Point3 gridPoint = basis.origin + basis.x * gx + basis.y * gy;
    out.push_back(EditorSnapCandidate{
            .world = gridPoint,
            .kind = EditorSnapKind::Grid,
            .dependency = EditorPointDependencyKind::Grid,
            .priority = kGridPriority,
            .distance = query.workPoint->distance(gridPoint),
            .worldDistance = query.workPoint->distance(gridPoint),
    });
}

void addAxisCandidate(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out) {
    const EditorSnapQuery& query = input.query;
    if (!query.workPoint || !query.pointPolicy.allowAxisConstraint || !query.pointPolicy.axisAnchor ||
        !query.snapSettings.enabled || !query.snapSettings.enableAxisConstraint ||
        !query.event.hasModifier(engine::KeyModifier::Shift)) {
        return;
    }

    const PlaneBasis basis = makePlaneBasis(query.workPlane);
    const math::Point3 anchor = *query.pointPolicy.axisAnchor;
    const math::Vec3 delta = *query.workPoint - anchor;

    const math::Point3 axisX = anchor + basis.x * delta.dot(basis.x);
    const math::Point3 axisY = anchor + basis.y * delta.dot(basis.y);
    const double distX = query.workPoint->distance(axisX);
    const double distY = query.workPoint->distance(axisY);
    const math::Point3 point = distX <= distY ? axisX : axisY;

    out.push_back(EditorSnapCandidate{
            .world = point,
            .kind = EditorSnapKind::Axis,
            .dependency = EditorPointDependencyKind::Axis,
            .priority = kAxisPriority,
            .distance = std::min(distX, distY),
            .worldDistance = std::min(distX, distY),
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
    const EditorSnapQuery& query = input.query;
    if (query.pickWorld.available() || !query.snapSettings.enabled || !query.snapSettings.enableGeometrySnap ||
        !query.pointPolicy.allowGeometry || !query.primaryPickHit || !query.primaryPickHit->valid()) {
        return;
    }

    const EditorPickHit& hit = *query.primaryPickHit;
    if (hit.kind == EditorPickHitKind::Edge) {
        addEdgeCandidates(hit, query.snapSettings, out);
        return;
    }

    if (hit.kind == EditorPickHitKind::Face && query.snapSettings.enableFacePointSnap && hit.hasWorldPoint) {
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
    EditorSceneSnapProvider::collect(input.query, out);
    addGeometryCandidates(input, out);
}

}  // namespace mulan::editor
