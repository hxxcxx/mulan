#include "editor_scene_snap_provider.h"

#include <mulan/engine/render/camera/camera.h>
#include <mulan/view/render_scene.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulan::app {
namespace {

constexpr double kFacePointPriority = 1.5;
constexpr double kEdgeNearestPriority = 2.0;
constexpr double kMidpointPriority = 3.0;
constexpr double kEndpointPriority = 4.0;

EditorGeometryDependency geometryDependencyFromPick(const view::RenderScene::PickResult& pick, EditorPickHitKind kind,
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

void addPointCandidate(const EditorSnapQuery& query, const view::RenderScene::PickResult& pick, EditorSnapKind kind,
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

void addEdgeCandidates(const EditorSnapQuery& query, const view::RenderScene::PickResult& pick,
                       std::vector<EditorSnapCandidate>& out) {
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

void addFaceCandidate(const EditorSnapQuery& query, const view::RenderScene::PickResult& pick,
                      std::vector<EditorSnapCandidate>& out) {
    if (!query.snapSettings.enableFacePointSnap || !pick.hasWorldPoint) {
        return;
    }

    addPointCandidate(query, pick, EditorSnapKind::Face, EditorPickHitKind::Face, pick.parameter, kFacePointPriority,
                      pick.worldPoint, out);
}

void addPickCandidates(const EditorSnapQuery& query, const view::RenderScene::PickResult& pick,
                       std::vector<EditorSnapCandidate>& out) {
    switch (pick.kind) {
    case view::RenderScene::PickHitKind::Edge: addEdgeCandidates(query, pick, out); break;
    case view::RenderScene::PickHitKind::Face: addFaceCandidate(query, pick, out); break;
    case view::RenderScene::PickHitKind::Object:
    case view::RenderScene::PickHitKind::Vertex:
    case view::RenderScene::PickHitKind::Curve:
    case view::RenderScene::PickHitKind::None: break;
    }
}

}  // namespace

void EditorSceneSnapProvider::collect(const EditorSnapQuery& query, std::vector<EditorSnapCandidate>& out) {
    if (!query.snapSettings.enabled || !query.snapSettings.enableGeometrySnap || !query.pointPolicy.allowGeometry ||
        !query.renderScene || !query.hasCursorRay) {
        return;
    }

    std::vector<view::RenderScene::PickResult> picks;
    query.renderScene->collectPickCandidates(query.cursorRay, query.toleranceWorld, picks);
    for (const view::RenderScene::PickResult& pick : picks) {
        addPickCandidates(query, pick, out);
    }
}

}  // namespace mulan::app
