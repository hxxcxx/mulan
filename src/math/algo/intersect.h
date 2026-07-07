/**
 * @file intersect.h
 * @brief 基础几何类型求交与距离查询 — 直接实现（路线 A，闭式解）
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 覆盖范围：
 *  射线求交：ray-AABB(Slab)、ray-Sphere、ray-Plane、ray-Triangle
 *  线段/直线：segment-Plane、segment-segment(2D/3D)、line-line(2D/3D)、line-plane
 *  体积相交：aabb-aabb、sphere-sphere、aabb-sphere
 *  距离：point-segment、point-plane、point-line
 *
 * 不涉及参数曲线(curve)/曲面求交（留作后续独立模块）。
 * 所有几何比较走 Tolerance 系统。
 */
#pragma once

#include "../math_export.h"

#include "../linalg/vec2.h"
#include "../linalg/vec3.h"
#include "../geom/point.h"
#include "../geom/aabb.h"
#include "../geom/sphere.h"
#include "../geom/plane.h"
#include "../geom/line.h"
#include "hit.h"
#include "../scalar/tolerance.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mulan::math {

// 注：Ray3 定义于 Line.h，此处直接引用。

// ============================================================
// 射线求交
// ============================================================

/// 射线 - AABB（Slab 算法）。命中时 t 为沿射线方向参数。
MATH_API Hit3 intersect(const Ray3& ray, const AABB3& box, const Tolerance& tol = defaultTolerance());

/// 射线 - 球
MATH_API Hit3 intersect(const Ray3& ray, const Sphere3& s);

/// 射线 - 平面
MATH_API Hit3 intersect(const Ray3& ray, const Plane3& plane, const Tolerance& tol = defaultTolerance());

/// 射线 - 三角形（Möller–Trumbore），返回重心坐标于 bc
MATH_API Hit3 intersect(const Ray3& ray, const Point3& v0, const Point3& v1, const Point3& v2, Vec3* bc = nullptr,
                        const Tolerance& tol = defaultTolerance());

// ============================================================
// 线段 / 直线
// ============================================================

/// 线段 - 平面：返回线段参数 t∈[0,1]
MATH_API Hit3 intersect(const Segment3& seg, const Plane3& plane, const Tolerance& tol = defaultTolerance());

/// 2D 线段 - 线段：返回各自参数（sa on segA, sb on segB）
MATH_API bool intersect(const Segment2& a, const Segment2& b, double* sa = nullptr, double* sb = nullptr);

/// 3D 直线 - 直线：返回交点（共面且不平行时）。返回 hit=false 表示平行/异面。
MATH_API Hit3 intersect(const Line3& la, const Line3& lb, const Tolerance& tol = defaultTolerance());

/// 直线 - 平面
MATH_API Hit3 intersect(const Line3& line, const Plane3& plane, const Tolerance& tol = defaultTolerance());

// ============================================================
// 体积相交
// ============================================================
//   aabb-aabb / sphere-sphere / aabb-sphere 已作为成员方法提供：
//   AABB3::intersects(AABB3), Sphere3::intersects(Sphere3),
//   Sphere3::intersects(AABB3)。此处不再重复。

// ============================================================
// 距离查询
// ============================================================

/// 点到线段最近点（返回线段参数 t∈[0,1]）
MATH_API double closestParam(const Point3& p, const Segment3& seg);

inline Point3 closestPoint(const Point3& p, const Segment3& seg) {
    return seg.pointAt(closestParam(p, seg));
}

inline double distance(const Point3& p, const Segment3& seg) {
    return p.distance(closestPoint(p, seg));
}

/// 点到平面距离（绝对值）
inline double distance(const Point3& p, const Plane3& plane) {
    return std::abs(plane.signedDistance(p));
}

/// 点到直线距离
inline double distance(const Point3& p, const Line3& line) {
    return line.direction.cross(p - line.origin).length();  // |dir| 应为 1
}
inline double distance(const Point3& p, const Ray3& ray) {
    Vec3 w = p - ray.origin;
    double proj = w.dot(ray.direction);
    if (proj <= 0.0)
        return w.length();                   // 在射线反方向
    return ray.direction.cross(w).length();  // |dir| 应为 1
}

}  // namespace mulan::math
