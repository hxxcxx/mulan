/**
 * @file editor_input.h
 * @brief 定义编辑工具使用的结构化输入。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/engine/interaction/input_event.h>
#include <mulan/engine/interaction/work_plane.h>
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace mulan::engine {
class Camera;
}

namespace mulan::view {
class RenderScene;
}

namespace mulan::app {

enum class EditorPointSource {
    None,
    WorkPlane,
    Snap,
    Pick,
};

enum class EditorPickHitKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Curve,
};

enum class EditorSnapKind {
    None,
    WorkPlane,
    Vertex,
    Midpoint,
    Edge,
    Face,
    Curve,
    Grid,
    Axis,
    Depth,
};

enum class EditorPointDependencyKind {
    None,
    WorkPlane,
    Geometry,
    Grid,
    Axis,
    Depth,
};

struct EditorGeometryDependency {
    scene::EntityId entity = scene::EntityId::invalid();
    uint32_t pickId = 0;
    EditorPickHitKind hitKind = EditorPickHitKind::None;
    double distance = 0.0;
    size_t sourceDrawableIndex = 0;
    size_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    double parameter = 0.0;
    double toleranceWorld = 0.0;

    bool valid() const { return static_cast<bool>(entity); }
};

struct EditorPickHit {
    scene::EntityId entity = scene::EntityId::invalid();
    uint32_t pickId = 0;
    EditorPickHitKind kind = EditorPickHitKind::None;
    double distance = 0.0;
    math::Point3 worldPoint;
    bool hasWorldPoint = false;
    math::Vec3 worldNormal;
    bool hasWorldNormal = false;
    size_t sourceDrawableIndex = 0;
    size_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    double parameter = 0.0;
    double toleranceWorld = 0.0;
    math::Point3 edgeStart;
    math::Point3 edgeEnd;
    bool hasEdgeSegment = false;
    math::Vec3 barycentric;
    bool hasBarycentric = false;

    bool valid() const { return static_cast<bool>(entity) && kind != EditorPickHitKind::None; }
};

struct EditorSnapCandidate {
    math::Point3 world;
    EditorSnapKind kind = EditorSnapKind::None;
    EditorPointDependencyKind dependency = EditorPointDependencyKind::None;
    std::optional<EditorGeometryDependency> geometry;
    double priority = 0.0;
    double distance = 0.0;
    double screenDistance = 0.0;
    double worldDistance = 0.0;

    bool dependsOnGeometry() const { return geometry.has_value(); }
};

struct EditorPointPolicy {
    bool allowWorkPlane = true;
    bool allowGeometry = true;
    bool preferGeometry = true;
    bool allowAxisConstraint = true;
    std::optional<math::Point3> axisAnchor;
};

struct EditorSnapSettings {
    bool enabled = true;
    bool enableGeometrySnap = true;
    bool enableEndpointSnap = true;
    bool enableMidpointSnap = true;
    bool enableEdgeNearestSnap = true;
    bool enableFacePointSnap = true;
    bool enableGridSnap = false;
    bool enableAxisConstraint = true;
    double snapToleranceWorld = 0.0;
    double snapTolerancePixels = 10.0;
    double markerSizePixels = 14.0;
    double gridSpacing = 1.0;
};

struct EditorSnapQuery {
    engine::InputEvent event;
    const engine::Camera* camera = nullptr;
    const ::mulan::view::RenderScene* renderScene = nullptr;
    engine::WorkPlane workPlane = engine::WorkPlane::worldXY();
    math::Ray3 cursorRay;
    double screenX = 0.0;
    double screenY = 0.0;
    std::optional<math::Point3> workPoint;
    std::optional<EditorPickHit> primaryPickHit;
    EditorPointPolicy pointPolicy;
    EditorSnapSettings snapSettings;
    double tolerancePixels = 10.0;
    double toleranceWorld = 0.0;
    bool hasCursor = false;
    bool hasCursorRay = false;
    bool workPlaneHit = false;
};

struct EditorPoint {
    math::Point3 world;
    EditorPointSource source = EditorPointSource::None;
    EditorPointDependencyKind dependency = EditorPointDependencyKind::None;
    EditorSnapKind snapKind = EditorSnapKind::None;
    std::optional<EditorGeometryDependency> geometry;

    bool dependsOnGeometry() const { return geometry.has_value(); }
    bool isFreeWorkPlanePoint() const {
        return source == EditorPointSource::WorkPlane && dependency == EditorPointDependencyKind::WorkPlane &&
               !geometry;
    }
};

struct EditorSnapResult {
    std::optional<EditorPoint> point;
    std::optional<EditorSnapCandidate> candidate;
    bool resolved = false;
};

struct EditorInput {
    engine::InputEvent event;
    math::Ray3 cursorRay;
    engine::WorkPlane workPlane = engine::WorkPlane::worldXY();
    double screenX = 0.0;
    double screenY = 0.0;
    std::optional<EditorPickHit> pickHit;
    std::vector<EditorSnapCandidate> snapCandidates;
    EditorSnapQuery snapQuery;
    EditorSnapResult snapResult;
    std::optional<EditorPoint> point;
    std::optional<math::Point3> workPoint;
    std::optional<math::Point3> axisAnchor;
    std::optional<EditorGeometryDependency> geometryDependency;
    bool hasCursor = false;
    bool hasCursorRay = false;
    bool workPlaneHit = false;
    bool pickTested = false;
    bool snapResolved = false;

    bool hasGeometryDependency() const { return geometryDependency.has_value(); }

    std::optional<math::Point3> worldPoint() const {
        if (point) {
            return point->world;
        }
        return std::nullopt;
    }
};

}  // namespace mulan::app
