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

#include "linalg/vec2.h"
#include "linalg/vec3.h"
#include "geom/aabb.h"
#include "geom/sphere.h"
#include "geom/plane.h"
#include "geom/line.h"
#include "algo/hit.h"
#include "scalar/tolerance.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mulan::math {

// 注：Ray3 定义于 Line.h，此处直接引用。

// ============================================================
// 射线求交
// ============================================================

/// 射线 - AABB（Slab 算法）。命中时 t 为沿射线方向参数。
inline Hit3 intersect(const Ray3& ray, const AABB3& box,
                      const Tolerance& tol = defaultTolerance()) {
    if (box.isEmpty(tol)) return Hit3::miss();

    double tMin = -std::numeric_limits<double>::max();
    double tMax =  std::numeric_limits<double>::max();

    for (int i = 0; i < 3; ++i) {
        double o = ray.origin[i];
        double d = ray.direction[i];
        double bMin = box.min[i];
        double bMax = box.max[i];

        if (std::abs(d) < 1e-15) {
            // 射线与该轴的 slab 平行，原点须在 slab 内
            if (o < bMin - tol.lengthEps || o > bMax + tol.lengthEps)
                return Hit3::miss();
        } else {
            double inv = 1.0 / d;
            double t1 = (bMin - o) * inv;
            double t2 = (bMax - o) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax + tol.lengthEps) return Hit3::miss();
        }
    }

    // 射线起点可能在盒内（tMin<0），取最近正参数
    double t = tMin >= 0.0 ? tMin : tMax;
    if (t < -tol.lengthEps) return Hit3::miss();  // 整个盒在射线反方向
    return Hit3::make(ray.pointAt(t), t);
}

/// 射线 - 球
inline Hit3 intersect(const Ray3& ray, const Sphere3& s) {
    if (!s.isValid()) return Hit3::miss();
    Vec3 oc = ray.origin - s.center;
    double a = ray.direction.dot(ray.direction);
    double b = 2.0 * oc.dot(ray.direction);
    double c = oc.dot(oc) - s.radius * s.radius;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return Hit3::miss();
    double sq = std::sqrt(disc);
    double t = (-b - sq) / (2.0 * a);
    if (t < 0.0) t = (-b + sq) / (2.0 * a);   // 取近交点，否则取远交点
    if (t < 0.0) return Hit3::miss();
    return Hit3::make(ray.pointAt(t), t);
}

/// 射线 - 平面
inline Hit3 intersect(const Ray3& ray, const Plane3& plane,
                      const Tolerance& tol = defaultTolerance()) {
    double denom = plane.normal.dot(ray.direction);
    if (std::abs(denom) < 1e-15) return Hit3::miss();   // 平行
    double t = (plane.d - plane.normal.dot(ray.origin)) / denom;
    if (t < -tol.lengthEps) return Hit3::miss();
    return Hit3::make(ray.pointAt(t), t);
}

/// 射线 - 三角形（Möller–Trumbore），返回重心坐标于 bc
inline Hit3 intersect(const Ray3& ray, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                      Vec3* bc = nullptr,
                      const Tolerance& tol = defaultTolerance()) {
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 p = ray.direction.cross(e2);
    double det = e1.dot(p);
    if (std::abs(det) < 1e-15) return Hit3::miss();
    double inv = 1.0 / det;
    Vec3 tv = ray.origin - v0;
    double u = tv.dot(p) * inv;
    if (u < -tol.lengthEps || u > 1.0 + tol.lengthEps) return Hit3::miss();
    Vec3 q = tv.cross(e1);
    double v = ray.direction.dot(q) * inv;
    if (v < -tol.lengthEps || u + v > 1.0 + tol.lengthEps) return Hit3::miss();
    double t = e2.dot(q) * inv;
    if (t < -tol.lengthEps) return Hit3::miss();
    if (bc) *bc = Vec3(1.0 - u - v, u, v);
    return Hit3::make(ray.pointAt(t), t);
}

// ============================================================
// 线段 / 直线
// ============================================================

/// 线段 - 平面：返回线段参数 t∈[0,1]
inline Hit3 intersect(const Segment3& seg, const Plane3& plane,
                      const Tolerance& tol = defaultTolerance()) {
    Vec3 dir = seg.direction();
    double denom = plane.normal.dot(dir);
    if (std::abs(denom) < 1e-15) return Hit3::miss();   // 平行
    double t = (plane.d - plane.normal.dot(seg.start)) / denom;
    if (t < -tol.lengthEps || t > 1.0 + tol.lengthEps) return Hit3::miss();
    return Hit3::make(seg.pointAt(t), t);
}

/// 2D 线段 - 线段：返回各自参数（sa on segA, sb on segB）
inline bool intersect(const Segment2& a, const Segment2& b,
                      double* sa = nullptr, double* sb = nullptr) {
    Vec2 d1 = a.direction();
    Vec2 d2 = b.direction();
    double denom = d1.cross(d2);
    if (std::abs(denom) < 1e-15) return false;   // 平行/共线
    Vec3 diff3(a.start.x - b.start.x, a.start.y - b.start.y, 0.0);
    // 求解 sa, sb
    Vec3 d13(d1.x, d1.y, 0.0);
    Vec3 d23(d2.x, d2.y, 0.0);
    double s = Vec3(b.start.x - a.start.x, b.start.y - a.start.y, 0.0).cross(d23).z / denom;
    double t = Vec3(b.start.x - a.start.x, b.start.y - a.start.y, 0.0).cross(d13).z / denom;
    if (s < 0.0 || s > 1.0 || t < 0.0 || t > 1.0) return false;
    if (sa) *sa = s;
    if (sb) *sb = t;
    return true;
}

/// 3D 直线 - 直线：返回交点（共面且不平行时）。返回 hit=false 表示平行/异面。
inline Hit3 intersect(const Line3& la, const Line3& lb,
                      const Tolerance& tol = defaultTolerance()) {
    Vec3 u = la.direction;
    Vec3 v = lb.direction;
    Vec3 w = la.origin - lb.origin;
    double a = u.dot(u);
    double b = u.dot(v);
    double c = v.dot(v);
    double d = u.dot(w);
    double e = v.dot(w);
    double denom = a * c - b * b;
    if (std::abs(denom) < 1e-15) return Hit3::miss();   // 平行
    double sc = (b * e - c * d) / denom;
    // 最近距离检测：若两直线异面，则不算相交
    Vec3 pA = la.pointAt(sc);
    Vec3 pB = lb.pointAt((a * e - b * d) / denom);
    if ((pA - pB).lengthSq() > tol.lengthEps * tol.lengthEps)
        return Hit3::miss();   // 异面
    return Hit3::make(pA, sc);
}

/// 直线 - 平面
inline Hit3 intersect(const Line3& line, const Plane3& plane,
                      const Tolerance& tol = defaultTolerance()) {
    double denom = plane.normal.dot(line.direction);
    if (std::abs(denom) < 1e-15) return Hit3::miss();
    double t = (plane.d - plane.normal.dot(line.origin)) / denom;
    return Hit3::make(line.pointAt(t), t);
}

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
inline double closestParam(const Vec3& p, const Segment3& seg) {
    Vec3 dir = seg.direction();
    double lenSq = dir.lengthSq();
    if (lenSq < 1e-15) return 0.0;
    double t = (p - seg.start).dot(dir) / lenSq;
    return clamp(t, 0.0, 1.0);
}

inline Vec3 closestPoint(const Vec3& p, const Segment3& seg) {
    return seg.pointAt(closestParam(p, seg));
}

inline double distance(const Vec3& p, const Segment3& seg) {
    return p.distanceTo(closestPoint(p, seg));
}

/// 点到平面距离（绝对值）
inline double distance(const Vec3& p, const Plane3& plane) {
    return std::abs(plane.signedDistance(p));
}

/// 点到直线距离
inline double distance(const Vec3& p, const Line3& line) {
    return line.direction.cross(p - line.origin).length();   // |dir| 应为 1
}
inline double distance(const Vec3& p, const Ray3& ray) {
    Vec3 w = p - ray.origin;
    double proj = w.dot(ray.direction);
    if (proj <= 0.0) return w.length();          // 在射线反方向
    return ray.direction.cross(w).length();      // |dir| 应为 1
}

} // namespace mulan::math
