/**
 * @file AABB.h
 * @brief 轴对齐包围盒（2D / 3D）— 视锥裁剪、碰撞检测
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 空 box：min 各分量取 max 有限值、max 各分量取 min 有限值，使 isEmpty() 为 true。
 */
#pragma once

#include "Vec2.h"
#include "Vec3.h"
#include "Mat3.h"
#include "Mat4.h"
#include "Tolerance.h"

#include <limits>

namespace mulan::geo {

// ============================================================
// AABB3
// ============================================================

struct AABB3 {
    Vec3 min{ std::numeric_limits<double>::max(),
              std::numeric_limits<double>::max(),
              std::numeric_limits<double>::max()};
    Vec3 max{-std::numeric_limits<double>::max(),
             -std::numeric_limits<double>::max(),
             -std::numeric_limits<double>::max()};

    constexpr AABB3() = default;
    constexpr AABB3(const Vec3& min_, const Vec3& max_) : min(min_), max(max_) {}

    static AABB3 empty() { return AABB3{}; }

    static AABB3 fromCenterExtents(const Vec3& center, const Vec3& extents) {
        return AABB3(center - extents, center + extents);
    }

    // ---------- 状态 ----------
    bool isEmpty(const Tolerance& tol = defaultTolerance()) const {
        return min.x > max.x + tol.lengthEps
            || min.y > max.y + tol.lengthEps
            || min.z > max.z + tol.lengthEps;
    }

    void reset() { *this = empty(); }

    // ---------- 扩展 ----------
    void expand(const Vec3& p) {
        min.x = geo::min(min.x, p.x); max.x = geo::max(max.x, p.x);
        min.y = geo::min(min.y, p.y); max.y = geo::max(max.y, p.y);
        min.z = geo::min(min.z, p.z); max.z = geo::max(max.z, p.z);
    }
    void expand(const AABB3& b) {
        if (b.isEmpty()) return;
        min.x = geo::min(min.x, b.min.x); max.x = geo::max(max.x, b.max.x);
        min.y = geo::min(min.y, b.min.y); max.y = geo::max(max.y, b.max.y);
        min.z = geo::min(min.z, b.min.z); max.z = geo::max(max.z, b.max.z);
    }

    // ---------- 查询 ----------
    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 extents() const { return (max - min) * 0.5; }
    Vec3 size() const { return max - min; }

    bool contains(const Vec3& p, const Tolerance& tol = defaultTolerance()) const {
        return p.x >= min.x - tol.lengthEps && p.x <= max.x + tol.lengthEps
            && p.y >= min.y - tol.lengthEps && p.y <= max.y + tol.lengthEps
            && p.z >= min.z - tol.lengthEps && p.z <= max.z + tol.lengthEps;
    }

    bool intersects(const AABB3& b) const {
        return (min.x <= b.max.x && max.x >= b.min.x)
            && (min.y <= b.max.y && max.y >= b.min.y)
            && (min.z <= b.max.z && max.z >= b.min.z);
    }

    /// 经 Mat4 变换后的新 AABB（取 8 角点的变换后包围）
    AABB3 transformed(const Mat4& m) const {
        if (isEmpty()) return empty();
        AABB3 r;
        for (int i = 0; i < 8; ++i) {
            Vec3 corner((i & 1) ? max.x : min.x,
                        (i & 2) ? max.y : min.y,
                        (i & 4) ? max.z : min.z);
            r.expand(transformPoint(m, corner));
        }
        return r;
    }
};

// ============================================================
// AABB2
// ============================================================

struct AABB2 {
    Vec2 min{ std::numeric_limits<double>::max(),
              std::numeric_limits<double>::max()};
    Vec2 max{-std::numeric_limits<double>::max(),
             -std::numeric_limits<double>::max()};

    constexpr AABB2() = default;
    constexpr AABB2(const Vec2& min_, const Vec2& max_) : min(min_), max(max_) {}

    static AABB2 empty() { return AABB2{}; }

    bool isEmpty(const Tolerance& tol = defaultTolerance()) const {
        return min.x > max.x + tol.lengthEps || min.y > max.y + tol.lengthEps;
    }
    void reset() { *this = empty(); }

    void expand(const Vec2& p) {
        min.x = geo::min(min.x, p.x); max.x = geo::max(max.x, p.x);
        min.y = geo::min(min.y, p.y); max.y = geo::max(max.y, p.y);
    }
    void expand(const AABB2& b) {
        if (b.isEmpty()) return;
        min.x = geo::min(min.x, b.min.x); max.x = geo::max(max.x, b.max.x);
        min.y = geo::min(min.y, b.min.y); max.y = geo::max(max.y, b.max.y);
    }

    Vec2 center() const { return (min + max) * 0.5; }
    Vec2 size() const { return max - min; }

    bool contains(const Vec2& p, const Tolerance& tol = defaultTolerance()) const {
        return p.x >= min.x - tol.lengthEps && p.x <= max.x + tol.lengthEps
            && p.y >= min.y - tol.lengthEps && p.y <= max.y + tol.lengthEps;
    }
    bool intersects(const AABB2& b) const {
        return (min.x <= b.max.x && max.x >= b.min.x)
            && (min.y <= b.max.y && max.y >= b.min.y);
    }

    /// 经 2D 矩阵变换后的新 AABB（取 4 角变换后包围）
    AABB2 transformed(const Mat3& m) const {
        if (isEmpty()) return empty();
        AABB2 r;
        Vec2 corners[4] = { {min.x, min.y}, {max.x, min.y},
                            {max.x, max.y}, {min.x, max.y} };
        for (const Vec2& c : corners) {
            Vec3 ct = m * Vec3(c.x, c.y, 1.0);
            r.expand(Vec2(ct.x, ct.y));
        }
        return r;
    }
};

} // namespace mulan::geo
