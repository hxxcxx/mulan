/**
 * @file aabb.h
 * @brief 轴对齐包围盒，用于视锥体裁剪与碰撞检测
 * @author hxxcxx
 * @date 2026-04-20
 */

#pragma once

#include "math.h"

#include <limits>

namespace mulan::engine {

struct AABB {
    Vec3 min = {std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()};
    Vec3 max = {std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest()};

    static AABB empty() { return {}; }

    static AABB fromCenterExtents(const Vec3& center, const Vec3& extents) {
        return {center - extents, center + extents};
    }

    // --- 操作 ---

    bool isEmpty() const {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    void reset() {
        min = {std::numeric_limits<double>::max(),
               std::numeric_limits<double>::max(),
               std::numeric_limits<double>::max()};
        max = {std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::lowest()};
    }

    void expand(const Vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void expand(const AABB& b) {
        if (b.isEmpty()) return;
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }

    // --- 查询 ---

    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 extents() const { return (max - min) * 0.5; }
    Vec3 size() const { return max - min; }

    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const AABB& b) const {
        return (min.x <= b.max.x && max.x >= b.min.x)
            && (min.y <= b.max.y && max.y >= b.min.y)
            && (min.z <= b.max.z && max.z >= b.min.z);
    }

    AABB transformed(const Mat4& m) const {
        if (isEmpty()) return empty();

        AABB result;
        for (int i = 0; i < 8; ++i) {
            Vec3 corner(
                (i & 1) ? max.x : min.x,
                (i & 2) ? max.y : min.y,
                (i & 4) ? max.z : min.z
            );
            result.expand(Vec3(m * Vec4(corner, 1.0)));
        }
        return result;
    }
};

} // namespace mulan::engine
