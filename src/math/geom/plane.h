/**
 * @file plane.h
 * @brief 平面（3D）— 法向 + 距离方程 n·p = d
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 存储为 n·p = d 形式：normal 单位化后，d 为原点到平面的有向距离。
 */
#pragma once

#include "../linalg/vec3.h"
#include "../linalg/mat4.h"
#include "point.h"
#include "../scalar/tolerance.h"

#include <cmath>

namespace mulan::math {

struct Plane3 {
    Vec3   normal{};   // 应为单位向量
    double d = 0.0;    // n·p = d

    constexpr Plane3() = default;
    constexpr Plane3(const Vec3& n, double d_) : normal(n), d(d_) {}

    /// 从一点 + 法向构造（法向会被归一化）
    static Plane3 fromPointNormal(const Vec3& point, const Vec3& n) {
        Vec3 unit = n.normalized();
        return Plane3(unit, unit.dot(point));
    }

    /// 从三个点构造（逆时针给出，法向由右手定则确定）
    static Plane3 fromTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
        return fromPointNormal(a, (b - a).cross(c - a));
    }

    // ---------- 查询 ----------

    /// 有符号距离：>0 法向侧，<0 反向侧，=0 在平面上
    double signedDistance(const Vec3& p) const {
        return normal.dot(p) - d;
    }

    bool contains(const Vec3& p, const Tolerance& tol = defaultTolerance()) const {
        return std::abs(signedDistance(p)) <= tol.lengthEps;
    }

    /// 将 p 投影到平面
    Vec3 project(const Vec3& p) const {
        return p - normal * signedDistance(p);
    }

    /// 经矩阵变换：法向用逆转置（transformedAsNormal），平面上一点用点变换
    Plane3 transformed(const Mat4& m) const {
        Vec3 newNormal = normal.transformedAsNormal(m);   // 已归一化
        // 平面上一点 = normal * d，经点变换
        Vec3 pointOnPlane = normal * d;
        Vec3 newPoint = transformPoint(m, pointOnPlane);
        return Plane3(newNormal, newNormal.dot(newPoint));
    }
};

} // namespace mulan::math
