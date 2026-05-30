/**
 * @file BoundingSphere.h
 * @brief 包围球，用于快速剔除与碰撞检测
 * @author hxxcxx
 * @date 2026-04-20
 */

#pragma once

#include "AABB.h"

#include <cmath>
#include <algorithm>

namespace mulan::engine {

struct BoundingSphere {
    Vec3   center;
    double radius = -1.0;  // 负值表示无效

    // --- 工厂 ---

    static BoundingSphere invalid() { return {}; }

    static BoundingSphere fromCenterRadius(const Vec3& c, double r) {
        return {c, r};
    }

    static BoundingSphere fromAABB(const AABB& box) {
        if (box.isEmpty()) return invalid();
        Vec3 c = box.center();
        return {c, glm::distance(c, box.max)};
    }

    // --- 状态 ---

    bool isValid() const { return radius >= 0.0; }

    void reset() { center = Vec3(0.0); radius = -1.0; }

    // --- 扩展 ---

    void expand(const Vec3& point) {
        if (!isValid()) {
            center = point;
            radius = 0.0;
            return;
        }
        Vec3 diff = point - center;
        double dist = glm::length(diff);
        if (dist <= radius) return;

        double newRadius = (radius + dist) * 0.5;
        double shift = newRadius - radius;
        center += diff * (shift / dist);
        radius = newRadius;
    }

    void expand(const BoundingSphere& other) {
        if (!other.isValid()) return;
        if (!isValid()) { *this = other; return; }

        Vec3 diff  = other.center - center;
        double dist = glm::length(diff);

        if (dist + other.radius <= radius) return;
        if (dist + radius <= other.radius) { *this = other; return; }

        double newRadius = (radius + dist + other.radius) * 0.5;
        if (dist > 1e-12) {
            center += diff * ((newRadius - radius) / dist);
        }
        radius = newRadius;
    }

    void expand(const AABB& box) {
        if (box.isEmpty()) return;
        expand(BoundingSphere::fromAABB(box));
    }

    // --- 查询 ---

    bool contains(const Vec3& point) const {
        if (!isValid()) return false;
        return glm::length2(point - center) <= radius * radius;
    }

    bool intersects(const BoundingSphere& other) const {
        if (!isValid() || !other.isValid()) return false;
        double r = radius + other.radius;
        return glm::length2(center - other.center) <= r * r;
    }

    bool intersects(const AABB& box) const {
        if (!isValid() || box.isEmpty()) return false;
        Vec3 closest = glm::max(box.min, glm::min(box.max, center));
        return glm::length2(closest - center) <= radius * radius;
    }

    // --- 变换 ---

    BoundingSphere transformed(const Mat4& m) const {
        if (!isValid()) return invalid();
        Vec3 newCenter = Vec3(m * Vec4(center, 1.0));
        // 从矩阵列向量提取缩放因子
        double sx = glm::length(Vec3(m[0]));
        double sy = glm::length(Vec3(m[1]));
        double sz = glm::length(Vec3(m[2]));
        double maxScale = std::max({sx, sy, sz});
        return {newCenter, radius * maxScale};
    }
};

} // namespace mulan::engine
