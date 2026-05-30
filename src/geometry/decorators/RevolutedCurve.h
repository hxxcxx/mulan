/**
 * @file RevolutedCurve.h
 * @brief 旋转曲面 (曲线绕轴旋转)
 *
 * 基于 truck-geometry::decorators::RevolutedCurve。
 * subs(u, v) = rotation_matrix(u) * (curve.subs(v) - origin) + origin
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricSurface.h"
#include "../Export.h"
#include <cmath>

namespace mulan::geometry {

/// 旋转曲面: 3D 曲线绕轴旋转
template<typename Curve>
class RevolutedCurve : public ParametricSurface3D {
public:
    RevolutedCurve() = default;
    RevolutedCurve(Curve curve, Point3 origin, Vector3 axis)
        : curve_(std::move(curve))
        , origin_(std::move(origin))
        , axis_(glm::normalize(axis)) {}

    const Curve& generatingCurve() const { return curve_; }
    const Point3& rotationOrigin() const { return origin_; }
    const Vector3& rotationAxis() const { return axis_; }

    Point3 subs(double u, double v) const override {
        Point3 p = curve_.subs(v);
        // 绕 axis_ 旋转角度 u
        Vector3 d = p - origin_;
        double cos_u = std::cos(u), sin_u = std::sin(u);
        Vector3 result = d * cos_u + glm::cross(axis_, d) * sin_u +
                         axis_ * glm::dot(axis_, d) * (1.0 - cos_u); // Rodrigues 公式
        return origin_ + result;
    }

    Vector3 uder(double u, double v) const override {
        Point3 p = curve_.subs(v);
        Vector3 d = p - origin_;
        double cos_u = std::cos(u), sin_u = std::sin(u);
        return -d * sin_u + glm::cross(axis_, d) * cos_u +
               axis_ * glm::dot(axis_, d) * sin_u;
    }

    Vector3 vder(double u, double v) const override {
        Vector3 dp = curve_.der(v);
        double cos_u = std::cos(u), sin_u = std::sin(u);
        return dp * cos_u + glm::cross(axis_, dp) * sin_u +
               axis_ * glm::dot(axis_, dp) * (1.0 - cos_u);
    }

    Vector3 uuder(double u, double v) const override {
        Point3 p = curve_.subs(v);
        Vector3 d = p - origin_;
        double cos_u = std::cos(u), sin_u = std::sin(u);
        return -d * cos_u - glm::cross(axis_, d) * sin_u +
               axis_ * glm::dot(axis_, d) * cos_u;
    }

    Vector3 uvder(double u, double v) const override {
        Vector3 dp = curve_.der(v);
        double cos_u = std::cos(u), sin_u = std::sin(u);
        return -dp * sin_u + glm::cross(axis_, dp) * cos_u +
               axis_ * glm::dot(axis_, dp) * sin_u;
    }

    Vector3 vvder(double u, double v) const override {
        Vector3 ddp = curve_.der2(v);
        double cos_u = std::cos(u), sin_u = std::sin(u);
        return ddp * cos_u + glm::cross(axis_, ddp) * sin_u +
               axis_ * glm::dot(axis_, ddp) * (1.0 - cos_u);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        // 0~2 阶解析实现
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (m == 1 && n == 0) return uder(u, v);
        if (m == 0 && n == 1) return vder(u, v);
        if (m == 2 && n == 0) return uuder(u, v);
        if (m == 1 && n == 1) return uvder(u, v);
        if (m == 0 && n == 2) return vvder(u, v);
        // 高阶: 数值差分兜底
        return numericalDerMN(m, n, u, v);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        auto cr = curve_.parameterRange();
        return {
            {Bound{BoundKind::Included, 0.0}, Bound{BoundKind::Excluded, TWO_PI}},
            cr
        };
    }

    std::optional<double> uPeriod() const override { return TWO_PI; }

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        origin_ = Point3(mat * glm::dvec4(origin_, 1.0));
        axis_ = Vector3(mat * glm::dvec4(axis_, 0.0));
        if constexpr (requires { curve_.transformBy(mat); }) {
            curve_.transformBy(mat);
        }
    }

private:
    Curve curve_;
    Point3 origin_;
    Vector3 axis_;
};

} // namespace mulan::geometry
