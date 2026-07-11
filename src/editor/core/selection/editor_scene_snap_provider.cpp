#include "editor_scene_snap_provider.h"

#include <mulan/render/camera/camera.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::editor {
namespace {

constexpr double kFacePointPriority = 1.5;
constexpr double kEdgeNearestPriority = 2.0;
constexpr double kCurveNearestPriority = 2.2;
constexpr double kMidpointPriority = 3.0;
constexpr double kCenterPriority = 3.15;
constexpr double kTangentPriority = 3.25;
constexpr double kEndpointPriority = 4.0;

EditorGeometryDependency geometryDependencyFromPick(const EditorPickHit& pick, EditorPickHitKind kind,
                                                    double parameter) {
    return EditorGeometryDependency{
        .entity = pick.entity,
        .pickId = pick.pickId,
        .hitKind = kind,
        .distance = pick.distance,
        .sourceDrawableIndex = pick.sourceDrawableIndex,
        .primitiveIndex = pick.primitiveIndex,
        .hasPrimitiveIndex = pick.hasPrimitiveIndex,
        .parameter = parameter,
        .toleranceWorld = pick.toleranceWorld,
    };
}

std::optional<math::Vec2> projectToScreen(const engine::Camera& camera, const math::Point3& point) {
    if (camera.width() <= 0 || camera.height() <= 0) {
        return std::nullopt;
    }

    math::Vec4 clip = camera.viewProjectionMatrix() * math::Vec4(point.asVec(), 1.0);
    if (std::abs(clip.w) <= 1.0e-12) {
        return std::nullopt;
    }

    clip /= clip.w;
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z)) {
        return std::nullopt;
    }

    return math::Vec2((clip.x * 0.5 + 0.5) * static_cast<double>(camera.width()),
                      (0.5 - clip.y * 0.5) * static_cast<double>(camera.height()));
}

std::optional<double> screenDistance(const EditorSnapQuery& query, const math::Point3& point) {
    if (!query.camera) {
        return std::nullopt;
    }

    const auto screen = projectToScreen(*query.camera, point);
    if (!screen) {
        return std::nullopt;
    }
    return screen->distanceTo(math::Vec2(query.screenX, query.screenY));
}

double worldDistanceToWorkPoint(const EditorSnapQuery& query, const math::Point3& point) {
    return query.workPoint ? query.workPoint->distance(point) : 0.0;
}

bool insideTolerance(const EditorSnapQuery& query, double distancePixels) {
    return distancePixels <= std::max(0.0, query.tolerancePixels);
}

double signedAngleAround(const math::Vec3& from, const math::Vec3& to, const math::Vec3& normal) {
    const math::Vec3 a = from.normalizedOr(math::Vec3::unitX());
    const math::Vec3 b = to.normalizedOr(a);
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    return std::atan2(n.dot(a.cross(b)), a.dot(b));
}

std::optional<double> curveRangeParameter(const EditorPickHit& pick, const math::Point3& point) {
    if (!pick.hasCurveRange || !pick.hasCurveCircle) {
        return std::nullopt;
    }

    math::Vec3 direction = point - pick.curveCenter;
    direction -= pick.curveNormal * direction.dot(pick.curveNormal);
    direction = direction.normalizedOr(pick.curveStartDirection);

    double angle = signedAngleAround(pick.curveStartDirection, direction, pick.curveNormal);
    const double sweep = pick.curveSweepRadians;
    if (std::abs(sweep) <= 1.0e-12) {
        return 0.0;
    }

    if (sweep > 0.0) {
        while (angle < 0.0) {
            angle += math::kPi2;
        }
    } else {
        while (angle > 0.0) {
            angle -= math::kPi2;
        }
    }
    return angle / sweep;
}

bool pointWithinCurveRange(const EditorPickHit& pick, const math::Point3& point) {
    const auto parameter = curveRangeParameter(pick, point);
    if (!parameter) {
        return true;
    }
    return *parameter >= -1.0e-6 && *parameter <= 1.0 + 1.0e-6;
}

void addPointCandidate(const EditorSnapQuery& query, const EditorPickHit& pick, EditorSnapKind kind,
                       EditorPickHitKind dependencyKind, double parameter, double priority, const math::Point3& point,
                       std::vector<EditorSnapCandidate>& out) {
    const auto distancePixels = screenDistance(query, point);
    if (!distancePixels || !insideTolerance(query, *distancePixels)) {
        return;
    }

    const double worldDistance = worldDistanceToWorkPoint(query, point);
    out.push_back(EditorSnapCandidate{
            .world = point,
            .kind = kind,
            .dependency = EditorPointDependencyKind::Geometry,
            .geometry = geometryDependencyFromPick(pick, dependencyKind, parameter),
            .priority = priority,
            .distance = *distancePixels,
            .screenDistance = *distancePixels,
            .worldDistance = worldDistance,
    });
}

void addEdgeCandidates(const EditorSnapQuery& query, const EditorPickHit& pick, std::vector<EditorSnapCandidate>& out) {
    if (!pick.hasWorldPoint || !pick.hasEdgeSegment) {
        return;
    }

    if (query.snapSettings.enableEndpointSnap) {
        addPointCandidate(query, pick, EditorSnapKind::Vertex, EditorPickHitKind::Vertex, 0.0, kEndpointPriority,
                          pick.edgeStart, out);
        addPointCandidate(query, pick, EditorSnapKind::Vertex, EditorPickHitKind::Vertex, 1.0, kEndpointPriority,
                          pick.edgeEnd, out);
    }

    if (query.snapSettings.enableMidpointSnap) {
        addPointCandidate(query, pick, EditorSnapKind::Midpoint, EditorPickHitKind::Edge, 0.5, kMidpointPriority,
                          math::lerp(pick.edgeStart, pick.edgeEnd, 0.5), out);
    }

    if (query.snapSettings.enableEdgeNearestSnap) {
        addPointCandidate(query, pick, EditorSnapKind::Edge, EditorPickHitKind::Edge, pick.parameter,
                          kEdgeNearestPriority, pick.worldPoint, out);
    }
}

void addFaceCandidate(const EditorSnapQuery& query, const EditorPickHit& pick, std::vector<EditorSnapCandidate>& out) {
    if (!query.snapSettings.enableFacePointSnap || !pick.hasWorldPoint) {
        return;
    }

    addPointCandidate(query, pick, EditorSnapKind::Face, EditorPickHitKind::Face, pick.parameter, kFacePointPriority,
                      pick.worldPoint, out);
}

void addTangentCandidates(const EditorSnapQuery& query, const EditorPickHit& pick,
                          std::vector<EditorSnapCandidate>& out) {
    if (!query.snapSettings.enableTangentSnap || !query.pointPolicy.axisAnchor || !pick.hasCurveCircle ||
        pick.curveRadius <= 0.0) {
        return;
    }

    const math::Point3 anchor = *query.pointPolicy.axisAnchor;
    math::Vec3 toAnchor = anchor - pick.curveCenter;
    toAnchor -= pick.curveNormal * toAnchor.dot(pick.curveNormal);
    const double distance = toAnchor.length();
    if (distance <= pick.curveRadius + 1.0e-9) {
        return;
    }

    const math::Vec3 u = toAnchor / distance;
    const math::Vec3 perpendicular = pick.curveNormal.cross(u).normalizedOr(math::Vec3::unitY());
    const double baseLength = (pick.curveRadius * pick.curveRadius) / distance;
    const double offsetLength =
            pick.curveRadius * std::sqrt(distance * distance - pick.curveRadius * pick.curveRadius) / distance;
    const math::Point3 tangentA = pick.curveCenter + u * baseLength + perpendicular * offsetLength;
    const math::Point3 tangentB = pick.curveCenter + u * baseLength - perpendicular * offsetLength;

    if (pointWithinCurveRange(pick, tangentA)) {
        const double parameter = curveRangeParameter(pick, tangentA).value_or(0.0);
        addPointCandidate(query, pick, EditorSnapKind::Tangent, EditorPickHitKind::Curve, parameter, kTangentPriority,
                          tangentA, out);
    }
    if (pointWithinCurveRange(pick, tangentB)) {
        const double parameter = curveRangeParameter(pick, tangentB).value_or(0.0);
        addPointCandidate(query, pick, EditorSnapKind::Tangent, EditorPickHitKind::Curve, parameter, kTangentPriority,
                          tangentB, out);
    }
}

void addCurveCandidates(const EditorSnapQuery& query, const EditorPickHit& pick,
                        std::vector<EditorSnapCandidate>& out) {
    if (query.snapSettings.enableEndpointSnap && pick.hasCurveEndpoints) {
        addPointCandidate(query, pick, EditorSnapKind::Vertex, EditorPickHitKind::Curve, 0.0, kEndpointPriority,
                          pick.curveStart, out);
        addPointCandidate(query, pick, EditorSnapKind::Vertex, EditorPickHitKind::Curve, 1.0, kEndpointPriority,
                          pick.curveEnd, out);
    }

    if (query.snapSettings.enableMidpointSnap && pick.hasCurveMidpoint && !pick.curveClosed) {
        addPointCandidate(query, pick, EditorSnapKind::Midpoint, EditorPickHitKind::Curve, 0.5, kMidpointPriority,
                          pick.curveMidpoint, out);
    }

    if (query.snapSettings.enableCenterSnap && pick.hasCurveCircle) {
        addPointCandidate(query, pick, EditorSnapKind::Center, EditorPickHitKind::Curve, 0.0, kCenterPriority,
                          pick.curveCenter, out);
    }

    if (query.snapSettings.enableCurveNearestSnap && pick.hasWorldPoint) {
        addPointCandidate(query, pick, EditorSnapKind::Curve, EditorPickHitKind::Curve, pick.parameter,
                          kCurveNearestPriority, pick.worldPoint, out);
    }

    addTangentCandidates(query, pick, out);
}

void addPickCandidates(const EditorSnapQuery& query, const EditorPickHit& pick, std::vector<EditorSnapCandidate>& out) {
    switch (pick.kind) {
    case EditorPickHitKind::Edge: addEdgeCandidates(query, pick, out); break;
    case EditorPickHitKind::Face: addFaceCandidate(query, pick, out); break;
    case EditorPickHitKind::Curve: addCurveCandidates(query, pick, out); break;
    case EditorPickHitKind::Object:
    case EditorPickHitKind::Vertex:
    case EditorPickHitKind::None: break;
    }
}

}  // namespace

void EditorSceneSnapProvider::collect(const EditorSnapQuery& query, std::vector<EditorSnapCandidate>& out) {
    if (!query.snapSettings.enabled || !query.snapSettings.enableGeometrySnap || !query.pointPolicy.allowGeometry ||
        !query.pickWorld.available() || !query.hasCursorRay) {
        return;
    }

    std::vector<EditorPickHit> picks;
    query.pickWorld.collectCandidates(query.cursorRay, query.toleranceWorld, picks);
    for (const EditorPickHit& pick : picks) {
        addPickCandidates(query, pick, out);
    }
}

}  // namespace mulan::editor
