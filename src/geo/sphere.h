/**
 * @file sphere.h
 * @brief 包围球（3D）— 快速剔除、碰撞检测
 * @author hxxcxx
 * @date 2026-06-29
 *
 * radius < 0 表示无效球。
 */
#pragma once

#include "vec3.h"
#include "mat4.h"
#include "aabb.h"
#include "tolerance.h"

#include <algorithm>
#include <cmath>

namespace mulan::geo {

struct Sphere3 {
    Vec3   center{};
    double radius = -1.0;   // 负值表示无效

    constexpr Sphere3() = default;
    constexpr Sphere3(const Vec3& c, double r) : center(c), radius(r) {}

    static Sphere3 invalid() { return Sphere3{}; }
    static Sphere3 fromCenterRadius(const Vec3& c, double r) { return Sphere3(c, r); }

    static Sphere3 fromAABB(const AABB3& box) {
        if (box.isEmpty()) return invalid();
        Vec3 c = box.center();
        return Sphere3(c, distance(c, box.max));
    }

    // ---------- 状态 ----------
    bool isValid() const { return radius >= 0.0; }
    void reset() { center = Vec3::zero(); radius = -1.0; }

    // ---------- 扩展 ----------
    void expand(const Vec3& p) {
        if (!isValid()) { center = p; radius = 0.0; return; }
        Vec3 d = p - center;
        double dist = d.length();
        if (dist <= radius) return;
        double newR = (radius + dist) * 0.5;
        double shift = newR - radius;
        center += d * (shift / dist);
        radius = newR;
    }

    void expand(const Sphere3& o) {
        if (!o.isValid()) return;
        if (!isValid()) { *this = o; return; }
        Vec3 d = o.center - center;
        double dist = d.length();
        if (dist + o.radius <= radius) return;
        if (dist + radius <= o.radius) { *this = o; return; }
        double newR = (radius + dist + o.radius) * 0.5;
        if (dist > 1e-12) center += d * ((newR - radius) / dist);
        radius = newR;
    }

    void expand(const AABB3& box) {
        if (box.isEmpty()) return;
        expand(Sphere3::fromAABB(box));
    }

    // ---------- 查询 ----------
    bool contains(const Vec3& p, const Tolerance& tol = defaultTolerance()) const {
        if (!isValid()) return false;
        double r = radius + tol.lengthEps;
        return (p - center).lengthSq() <= r * r;
    }

    bool intersects(const Sphere3& o) const {
        if (!isValid() || !o.isValid()) return false;
        double r = radius + o.radius;
        return (center - o.center).lengthSq() <= r * r;
    }

    bool intersects(const AABB3& box) const {
        if (!isValid() || box.isEmpty()) return false;
        // 求 box 上离 center 最近的点
        Vec3 closest(geo::max(box.min.x, geo::min(box.max.x, center.x)),
                     geo::max(box.min.y, geo::min(box.max.y, center.y)),
                     geo::max(box.min.z, geo::min(box.max.z, center.z)));
        return (closest - center).lengthSq() <= radius * radius;
    }

    // ---------- 变换 ----------
    Sphere3 transformed(const Mat4& m) const {
        if (!isValid()) return invalid();
        Vec3 newC = transformPoint(m, center);
        // 取矩阵列向量长度为缩放因子
        double sx = Vec3(m[0].x, m[0].y, m[0].z).length();
        double sy = Vec3(m[1].x, m[1].y, m[1].z).length();
        double sz = Vec3(m[2].x, m[2].y, m[2].z).length();
        double maxScale = std::max({sx, sy, sz});
        return Sphere3(newC, radius * maxScale);
    }
};

} // namespace mulan::geo
