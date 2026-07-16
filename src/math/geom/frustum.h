/**
 * @file frustum.h
 * @brief 视锥体提取与保守裁剪工具。
 *
 * Frustum3 保存六个朝向视锥内部的裁剪平面，沿用 Plane3 的 n·p = d 约定；
 * 位于视锥内部的点满足 signedDistance >= 0。
 * @author hxxcxx
 */
#pragma once

#include "aabb.h"
#include "../linalg/mat4.h"
#include "plane.h"
#include "sphere.h"
#include "../linalg/vec4.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace mulan::math {

enum class FrustumPlane : int { Left = 0, Right, Bottom, Top, Near, Far, Count };

struct Frustum3 {
    Plane3 planes[static_cast<int>(FrustumPlane::Count)]{};

    Plane3& operator[](FrustumPlane plane) { return planes[static_cast<int>(plane)]; }

    const Plane3& operator[](FrustumPlane plane) const { return planes[static_cast<int>(plane)]; }

    /**
     * 从视图投影矩阵提取视锥。矩阵包含非有限值、秩不足或无法形成有效裁剪平面时失败。
     *
     * 提取前按矩阵最大绝对元素统一缩放，既保持齐次裁剪平面不变，也避免极大或极小
     * 矩阵元素在求平面长度时上溢或下溢。
     */
    static std::optional<Frustum3> tryFromViewProjection(const Mat4& vp) {
        double rows[4][4]{};
        double matrixScale = 0.0;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                const double value = vp[column][row];
                if (!std::isfinite(value))
                    return std::nullopt;
                rows[row][column] = value;
                matrixScale = std::max(matrixScale, std::abs(value));
            }
        }
        if (!(matrixScale > 0.0))
            return std::nullopt;

        for (auto& row : rows) {
            for (double& value : row)
                value /= matrixScale;
        }

        // 视图投影矩阵必须满秩。秩检查使用独立副本做行、列均衡，避免大世界平移
        // 令有效主元因全局缩放落入次正规区；均衡只用于验证，不能改变下方平面提取
        // 所需的齐次行比例。消元阈值覆盖严格奇异矩阵留下的浮点残差，宁可 fail-open
        // 关闭一次裁剪，也不能把伪视锥交给空间索引造成漏绘。
        double rankRows[4][4]{};
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column)
                rankRows[row][column] = rows[row][column];
        }
        for (auto& rankRow : rankRows) {
            double rowScale = 0.0;
            for (double value : rankRow)
                rowScale = std::max(rowScale, std::abs(value));
            if (!(rowScale > 0.0))
                return std::nullopt;
            for (double& value : rankRow)
                value /= rowScale;
        }
        for (int column = 0; column < 4; ++column) {
            double columnScale = 0.0;
            for (const auto& rankRow : rankRows)
                columnScale = std::max(columnScale, std::abs(rankRow[column]));
            if (!(columnScale > 0.0))
                return std::nullopt;
            for (auto& rankRow : rankRows)
                rankRow[column] /= columnScale;
        }

        constexpr double rankTolerance = std::numeric_limits<double>::epsilon() * 64.0;
        for (int column = 0; column < 4; ++column) {
            int pivotRow = column;
            double pivotMagnitude = std::abs(rankRows[pivotRow][column]);
            for (int row = column + 1; row < 4; ++row) {
                const double candidateMagnitude = std::abs(rankRows[row][column]);
                if (candidateMagnitude > pivotMagnitude) {
                    pivotRow = row;
                    pivotMagnitude = candidateMagnitude;
                }
            }
            if (!(pivotMagnitude > rankTolerance))
                return std::nullopt;
            if (pivotRow != column) {
                for (int trailingColumn = column; trailingColumn < 4; ++trailingColumn)
                    std::swap(rankRows[column][trailingColumn], rankRows[pivotRow][trailingColumn]);
            }

            const double pivot = rankRows[column][column];
            for (int row = column + 1; row < 4; ++row) {
                const double factor = rankRows[row][column] / pivot;
                for (int trailingColumn = column + 1; trailingColumn < 4; ++trailingColumn)
                    rankRows[row][trailingColumn] -= factor * rankRows[column][trailingColumn];
            }
        }

        Frustum3 f;

        auto row = [&](int index) -> Vec4 {
            return Vec4(rows[index][0], rows[index][1], rows[index][2], rows[index][3]);
        };

        auto extract = [](const Vec4& coefficients) -> std::optional<Plane3> {
            const double length = std::hypot(coefficients.x, coefficients.y, coefficients.z);
            if (!std::isfinite(length) || !(length > 0.0))
                return std::nullopt;
            const Vec3 normal(coefficients.x / length, coefficients.y / length, coefficients.z / length);
            const double distance = -coefficients.w / length;
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
                !std::isfinite(distance)) {
                return std::nullopt;
            }
            return Plane3(normal, distance);
        };

        const Vec4 coefficients[] = { row(3) + row(0), row(3) - row(0), row(3) + row(1),
                                      row(3) - row(1), row(3) + row(2), row(3) - row(2) };
        for (int i = 0; i < static_cast<int>(FrustumPlane::Count); ++i) {
            const std::optional<Plane3> plane = extract(coefficients[i]);
            if (!plane)
                return std::nullopt;
            f.planes[i] = *plane;
        }

        return f;
    }

    /**
     * 兼容入口。合法矩阵与 tryFromViewProjection 结果一致；非法矩阵返回默认视锥，
     * 其零平面不会误裁有效包围体。需要区分失败原因的新代码应使用可失败入口。
     */
    static Frustum3 fromViewProjection(const Mat4& vp) { return tryFromViewProjection(vp).value_or(Frustum3{}); }

    bool contains(const Point3& point, const Tolerance& tol = defaultTolerance()) const {
        for (const Plane3& plane : planes) {
            const double distance = plane.signedDistance(point);
            if (std::isfinite(distance) && distance < -safeTolerance(tol) - numericalDistanceError(plane, point)) {
                return false;
            }
        }
        return true;
    }

    bool intersects(const AABB3& box, const Tolerance& tol = defaultTolerance()) const {
        if (box.isEmpty(tol))
            return false;

        for (const Plane3& plane : planes) {
            Point3 p = box.min;
            if (plane.normal.x >= 0.0)
                p.x = box.max.x;
            if (plane.normal.y >= 0.0)
                p.y = box.max.y;
            if (plane.normal.z >= 0.0)
                p.z = box.max.z;

            const double distance = plane.signedDistance(p);
            // 大世界坐标下 n·p 与 d 可能是两个大数相减。固定 1e-9 容差不足以
            // 覆盖浮点消减误差，因此叠加与运算量级相关的舍入上界；非有限结果
            // 按 fail-open 处理，视锥优化绝不能制造漏绘。
            if (std::isfinite(distance) && distance < -safeTolerance(tol) - numericalDistanceError(plane, p)) {
                return false;
            }
        }
        return true;
    }

    bool intersects(const Sphere3& sphere, const Tolerance& tol = defaultTolerance()) const {
        if (!sphere.isValid())
            return false;

        for (const Plane3& plane : planes) {
            const double distance = plane.signedDistance(sphere.center);
            if (std::isfinite(distance) &&
                distance < -sphere.radius - safeTolerance(tol) - numericalDistanceError(plane, sphere.center)) {
                return false;
            }
        }
        return true;
    }

private:
    static double safeTolerance(const Tolerance& tol) {
        return std::isfinite(tol.lengthEps) && tol.lengthEps > 0.0 ? tol.lengthEps : 0.0;
    }

    static double numericalDistanceError(const Plane3& plane, const Point3& point) {
        const double scale = std::abs(plane.normal.x * point.x) + std::abs(plane.normal.y * point.y) +
                             std::abs(plane.normal.z * point.z) + std::abs(plane.d);
        if (!std::isfinite(scale))
            return std::numeric_limits<double>::infinity();
        // 三次乘法、三次加减及编译器可能采用的 FMA 路径均由保守系数覆盖。
        return std::numeric_limits<double>::epsilon() * 16.0 * std::max(1.0, scale);
    }
};

using Frustum = Frustum3;

}  // namespace mulan::math
