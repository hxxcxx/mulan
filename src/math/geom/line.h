/**
 * @file line.h
 * @brief 直线 / 线段 / 射线（2D / 3D）
 * @author hxxcxx
 * @date 2026-06-29
 */
#pragma once

#include "linalg/vec2.h"
#include "linalg/vec3.h"
#include "linalg/mat3.h"
#include "linalg/mat4.h"

namespace mulan::math {

// ============================================================
// 3D
// ============================================================

/// 无限直线：点 + 单位方向
struct Line3 {
    Vec3 origin{};
    Vec3 direction{};   // 调用方应保证归一化

    constexpr Line3() = default;
    constexpr Line3(const Vec3& o, const Vec3& dir) : origin(o), direction(dir) {}

    Vec3 pointAt(double t) const { return origin + direction * t; }

    /// 经矩阵变换：origin 按点变换，direction 按方向变换
    Line3 transformed(const Mat4& m) const {
        return Line3(transformPoint(m, origin),
                     transformDir(m, direction).normalized());
    }
};

/// 线段：起点 + 终点
struct Segment3 {
    Vec3 start{};
    Vec3 end{};

    constexpr Segment3() = default;
    constexpr Segment3(const Vec3& s, const Vec3& e) : start(s), end(e) {}

    Vec3 direction() const { return end - start; }
    double length() const { return direction().length(); }
    double lengthSq() const { return direction().lengthSq(); }
    Vec3 pointAt(double t) const { return start + direction() * t; }  // t∈[0,1]

    /// 经矩阵变换（两端点按点变换）
    Segment3 transformed(const Mat4& m) const {
        return Segment3(transformPoint(m, start), transformPoint(m, end));
    }
};

/// 射线：原点 + 单位方向
struct Ray3 {
    Vec3 origin{};
    Vec3 direction{};   // 调用方应保证归一化

    constexpr Ray3() = default;
    constexpr Ray3(const Vec3& o, const Vec3& dir) : origin(o), direction(dir) {}

    Vec3 pointAt(double t) const { return origin + direction * t; }  // t≥0

    /// 经矩阵变换：origin 按点变换，direction 按方向变换
    Ray3 transformed(const Mat4& m) const {
        return Ray3(transformPoint(m, origin),
                    transformDir(m, direction).normalized());
    }
};

// ============================================================
// 2D
// ============================================================

struct Line2 {
    Vec2 origin{};
    Vec2 direction{};

    constexpr Line2() = default;
    constexpr Line2(const Vec2& o, const Vec2& dir) : origin(o), direction(dir) {}

    Vec2 pointAt(double t) const { return origin + direction * t; }

    /// 经 2D 矩阵变换（齐次：origin 用 w=1，direction 用 w=0）
    Line2 transformed(const Mat3& m) const {
        Vec3 o3 = m * Vec3(origin.x, origin.y, 1.0);
        Vec3 d3 = m * Vec3(direction.x, direction.y, 0.0);
        return Line2(Vec2(o3.x, o3.y), Vec2(d3.x, d3.y).normalized());
    }
};

struct Segment2 {
    Vec2 start{};
    Vec2 end{};

    constexpr Segment2() = default;
    constexpr Segment2(const Vec2& s, const Vec2& e) : start(s), end(e) {}

    Vec2 direction() const { return end - start; }
    double length() const { return direction().length(); }
    double lengthSq() const { return direction().lengthSq(); }
    Vec2 pointAt(double t) const { return start + direction() * t; }

    /// 经 2D 矩阵变换（两端点按点变换）
    Segment2 transformed(const Mat3& m) const {
        Vec3 s3 = m * Vec3(start.x, start.y, 1.0);
        Vec3 e3 = m * Vec3(end.x, end.y, 1.0);
        return Segment2(Vec2(s3.x, s3.y), Vec2(e3.x, e3.y));
    }
};

} // namespace mulan::math
