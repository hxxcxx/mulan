/**
 * @file picking_types.h
 * @brief 几何拾取子系统共享的内部类型与工具函数。
 * @author hxxcxx
 * @date 2026-07-11
 *
 * 本头文件仅供 view::scene_sync 内部拾取实现使用，不对外暴露。
 */
#pragma once

#include "../render_scene.h"
#include <mulan/math/math.h>

#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace mulan::view::detail {

/// std::visit 辅助：合并多个 lambda。
template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

/// 网格拾取的中间结果，后续转换为 RenderScene::PickResult。
struct MeshPickResult {
    bool tested = false;
    std::optional<double> distance;
    RenderScene::PickHitKind kind = RenderScene::PickHitKind::None;
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
    math::Point3 curveCenter;
    math::Vec3 curveNormal;
    double curveRadius = 0.0;
    bool hasCurveCircle = false;
    math::Point3 curveStart;
    math::Point3 curveEnd;
    math::Point3 curveMidpoint;
    bool hasCurveEndpoints = false;
    bool hasCurveMidpoint = false;
    bool curveClosed = false;
    math::Vec3 curveStartDirection;
    double curveSweepRadians = 0.0;
    bool hasCurveRange = false;
    math::Vec3 barycentric;
    bool hasBarycentric = false;
};

/// 射线与线段的最近点对。
struct RaySegmentClosest {
    double rayT = 0.0;
    double segmentT = 0.0;
    double distanceSq = std::numeric_limits<double>::max();
    math::Point3 rayPoint;
    math::Point3 segmentPoint;
};

/// 曲线上离射线最近的点。
struct CurveClosestPoint {
    math::Point3 point;
    double parameter = 0.0;
    double distanceToRay = std::numeric_limits<double>::max();
};

/// 按 amount 扩展 AABB（用于线拾取容差）。
inline math::AABB3 expandedBounds(const math::AABB3& bounds, double amount) {
    if (bounds.isEmpty() || amount <= 0.0) {
        return bounds;
    }
    math::AABB3 expanded = bounds;
    const math::Vec3 pad(amount, amount, amount);
    expanded.min -= pad;
    expanded.max += pad;
    return expanded;
}

/// 射线与线段的最近点对计算。
RaySegmentClosest closestRaySegment(const math::Ray3& ray, const math::Segment3& segment);

}  // namespace mulan::view::detail
