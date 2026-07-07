/**
 * @file intersect.cpp
 * @brief 基础几何类型求交与距离查询 — 实现
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "intersect.h"
#include "../math.h"

namespace mulan::math {

// ============================================================
// 射线求交
// ============================================================

Hit3 intersect(const Ray3& ray, const AABB3& box, const Tolerance& tol) {
    if (box.isEmpty(tol))
        return Hit3::miss();

    double tMin = -std::numeric_limits<double>::max();
    double tMax = std::numeric_limits<double>::max();

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
            if (t1 > t2)
                std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax + tol.lengthEps)
                return Hit3::miss();
        }
    }

    // 射线起点可能在盒内（tMin<0），取最近正参数
    double t = tMin >= 0.0 ? tMin : tMax;
    if (t < -tol.lengthEps)
        return Hit3::miss();  // 整个盒在射线反方向
    return Hit3::make(ray.pointAt(t), t);
}

Hit3 intersect(const Ray3& ray, const Sphere3& s) {
    if (!s.isValid())
        return Hit3::miss();
    Vec3 oc = ray.origin - s.center;
    double a = ray.direction.dot(ray.direction);
    double b = 2.0 * oc.dot(ray.direction);
    double c = oc.dot(oc) - s.radius * s.radius;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
        return Hit3::miss();
    double sq = std::sqrt(disc);
    double t = (-b - sq) / (2.0 * a);
    if (t < 0.0)
        t = (-b + sq) / (2.0 * a);  // 取近交点，否则取远交点
    if (t < 0.0)
        return Hit3::miss();
    return Hit3::make(ray.pointAt(t), t);
}

Hit3 intersect(const Ray3& ray, const Plane3& plane, const Tolerance& tol) {
    double denom = plane.normal.dot(ray.direction);
    if (std::abs(denom) < 1e-15)
        return Hit3::miss();  // 平行
    double t = (plane.d - plane.normal.dot(ray.origin.asVec())) / denom;
    if (t < -tol.lengthEps)
        return Hit3::miss();
    return Hit3::make(ray.pointAt(t), t);
}

Hit3 intersect(const Ray3& ray, const Point3& v0, const Point3& v1, const Point3& v2, Vec3* bc, const Tolerance& tol) {
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 p = ray.direction.cross(e2);
    double det = e1.dot(p);
    if (std::abs(det) < 1e-15)
        return Hit3::miss();
    double inv = 1.0 / det;
    Vec3 tv = ray.origin - v0;
    double u = tv.dot(p) * inv;
    if (u < -tol.lengthEps || u > 1.0 + tol.lengthEps)
        return Hit3::miss();
    Vec3 q = tv.cross(e1);
    double v = ray.direction.dot(q) * inv;
    if (v < -tol.lengthEps || u + v > 1.0 + tol.lengthEps)
        return Hit3::miss();
    double t = e2.dot(q) * inv;
    if (t < -tol.lengthEps)
        return Hit3::miss();
    if (bc)
        *bc = Vec3(1.0 - u - v, u, v);
    return Hit3::make(ray.pointAt(t), t);
}

// ============================================================
// 线段 / 直线
// ============================================================

Hit3 intersect(const Segment3& seg, const Plane3& plane, const Tolerance& tol) {
    Vec3 dir = seg.direction();
    double denom = plane.normal.dot(dir);
    if (std::abs(denom) < 1e-15)
        return Hit3::miss();  // 平行
    double t = (plane.d - plane.normal.dot(seg.start.asVec())) / denom;
    if (t < -tol.lengthEps || t > 1.0 + tol.lengthEps)
        return Hit3::miss();
    return Hit3::make(seg.pointAt(t), t);
}

bool intersect(const Segment2& a, const Segment2& b, double* sa, double* sb) {
    Vec2 d1 = a.direction();
    Vec2 d2 = b.direction();
    double denom = d1.cross(d2);
    if (std::abs(denom) < 1e-15)
        return false;  // 平行/共线
    // 求解 sa, sb（2D 叉乘标量形式：(b.start-a.start) × d_k / denom）
    Vec2 ab(b.start.x - a.start.x, b.start.y - a.start.y);
    double s = ab.cross(d2) / denom;
    double t = ab.cross(d1) / denom;
    if (s < 0.0 || s > 1.0 || t < 0.0 || t > 1.0)
        return false;
    if (sa)
        *sa = s;
    if (sb)
        *sb = t;
    return true;
}

Hit3 intersect(const Line3& la, const Line3& lb, const Tolerance& tol) {
    Vec3 u = la.direction;
    Vec3 v = lb.direction;
    Vec3 w = la.origin - lb.origin;
    double a = u.dot(u);
    double b = u.dot(v);
    double c = v.dot(v);
    double d = u.dot(w);
    double e = v.dot(w);
    double denom = a * c - b * b;
    if (std::abs(denom) < 1e-15)
        return Hit3::miss();  // 平行
    double sc = (b * e - c * d) / denom;
    // 最近距离检测：若两直线异面，则不算相交
    Point3 pA = la.pointAt(sc);
    Point3 pB = lb.pointAt((a * e - b * d) / denom);
    if ((pA - pB).lengthSq() > tol.lengthEps * tol.lengthEps)
        return Hit3::miss();  // 异面
    return Hit3::make(pA, sc);
}

Hit3 intersect(const Line3& line, const Plane3& plane, const Tolerance& tol) {
    double denom = plane.normal.dot(line.direction);
    if (std::abs(denom) < 1e-15)
        return Hit3::miss();
    double t = (plane.d - plane.normal.dot(line.origin.asVec())) / denom;
    return Hit3::make(line.pointAt(t), t);
}

// ============================================================
// 距离查询
// ============================================================

double closestParam(const Point3& p, const Segment3& seg) {
    Vec3 dir = seg.direction();
    double lenSq = dir.lengthSq();
    if (lenSq < 1e-15)
        return 0.0;
    double t = (p - seg.start).dot(dir) / lenSq;
    return clamp(t, 0.0, 1.0);
}

}  // namespace mulan::math
