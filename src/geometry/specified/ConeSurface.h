/**
 * @file ConeSurface.h
 * @brief 圆锥面
 *
 * 参数化: subs(u, v) = apex + v * (cos(α)*axis + sin(α)*(cos(u)*uAxis + sin(u)*vAxis))
 *
 * 其中 α 是半锥角，axis 是锥轴方向，uAxis/vAxis 是与 axis 正交的单位向量。
 * u ∈ [0, 2π), v ∈ [0, +∞)（v=0 是锥顶，v 增大远离锥顶）
 *
 * @author hxxcxx
 * @date 2026-05-28
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricSurface.h"
#include "../Export.h"
#include <cmath>

namespace mulan::geometry {

/// 圆锥面
class GEOMETRY_API ConeSurface : public ParametricSurface3D {
public:
    ConeSurface()
        : apex_(0.0)
        , axis_(0.0, 0.0, 1.0)
        , u_axis_(1.0, 0.0, 0.0)
        , v_axis_(0.0, 1.0, 0.0)
        , half_angle_(0.5) {}

    /// 从锥顶、轴、半锥角构造
    ConeSurface(Point3 apex, Vector3 axis, double halfAngle)
        : apex_(std::move(apex))
        , axis_(glm::normalize(axis))
        , half_angle_(halfAngle)
    {
        computeOrthogonalBasis();
    }

    /// 完整构造（指定正交基）
    ConeSurface(Point3 apex, Vector3 axis, double halfAngle,
                Vector3 uAxis, Vector3 vAxis)
        : apex_(std::move(apex))
        , axis_(glm::normalize(axis))
        , u_axis_(glm::normalize(uAxis))
        , v_axis_(glm::normalize(vAxis))
        , half_angle_(halfAngle) {}

    const Point3& apex() const { return apex_; }
    const Vector3& axis() const { return axis_; }
    double halfAngle() const { return half_angle_; }
    const Vector3& uAxis() const { return u_axis_; }
    const Vector3& vAxis() const { return v_axis_; }

    // --- ParametricSurface ---
    //
    // 令 sa = sin(α), ca = cos(α)
    // 令 D(u) = ca*axis + sa*(cos(u)*uAxis + sin(u)*vAxis)  (母线方向)
    // 令 D'(u) = sa*(-sin(u)*uAxis + cos(u)*vAxis)           (D 对 u 的导数)
    // 令 D''(u) = sa*(-cos(u)*uAxis - sin(u)*vAxis)          (D 对 u 的二阶导)
    //
    // subs(u, v) = apex + v * D(u)
    // ∂/∂u  = v * D'(u)
    // ∂/∂v  = D(u)
    // ∂²/∂u² = v * D''(u)
    // ∂²/∂u∂v = D'(u)
    // ∂²/∂v² = 0
    //
    // 高阶:
    //   ∂ⁿ/∂vⁿ = 0 (n >= 2)
    //   ∂^(m+n)/∂u^m ∂v^n:
    //     n >= 2: 0
    //     n == 1: D^(m)(u)  (m 阶导，不含 v 因子)
    //     n == 0: v * D^(m)(u)  (m >= 1 时)

    Point3 subs(double u, double v) const override {
        return apex_ + v * direction(u);
    }

    Vector3 uder(double u, double v) const override {
        return v * directionDer(u);
    }

    Vector3 vder(double u, double /*v*/) const override {
        return direction(u);
    }

    Vector3 uuder(double u, double v) const override {
        return v * directionDer2(u);
    }

    Vector3 uvder(double u, double /*v*/) const override {
        return directionDer(u);
    }

    Vector3 vvder(double /*u*/, double /*v*/) const override {
        return Vector3(0.0);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (n >= 2) return Vector3(0.0);  // v 方向二阶以上为零

        if (n == 1) {
            // ∂^(m+1)/∂u^m ∂v = D^(m)(u)
            return directionDerN(m, u);
        }

        // n == 0: ∂^m/∂u^m
        if (m == 0) return subs(u, v) - Point3(0.0);
        // m >= 1, n == 0: v * D^(m)(u)
        return v * directionDerN(m, u);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        Bound unbounded{BoundKind::Unbounded, 0.0};
        return {
            {Bound{BoundKind::Included, 0.0}, Bound{BoundKind::Excluded, TWO_PI}},
            {Bound{BoundKind::Included, 0.0}, unbounded}
        };
    }

    std::optional<double> uPeriod() const override { return TWO_PI; }

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        auto tp = [&](const Point3& p) { return Point3(mat * glm::dvec4(p, 1.0)); };
        auto tv = [&](const Vector3& v) { return Vector3(mat * glm::dvec4(v, 0.0)); };

        apex_ = tp(apex_);
        axis_ = glm::normalize(tv(axis_));
        u_axis_ = glm::normalize(tv(u_axis_));
        v_axis_ = glm::normalize(tv(v_axis_));
        // 半锥角在非均匀变换下会变化，这里做近似处理
    }

private:
    Point3 apex_;
    Vector3 axis_;
    Vector3 u_axis_;
    Vector3 v_axis_;
    double half_angle_;

    void computeOrthogonalBasis() {
        if (std::abs(axis_.x) < 0.9 && std::abs(axis_.y) < 0.9) {
            u_axis_ = glm::normalize(glm::cross(axis_, Vector3(0.0, 0.0, 1.0)));
        } else {
            u_axis_ = glm::normalize(glm::cross(axis_, Vector3(1.0, 0.0, 0.0)));
        }
        v_axis_ = glm::cross(axis_, u_axis_);
    }

    /// 母线方向 D(u) = ca*axis + sa*(cos(u)*uAxis + sin(u)*vAxis)
    Vector3 direction(double u) const {
        double sa = std::sin(half_angle_), ca = std::cos(half_angle_);
        double cu = std::cos(u), su = std::sin(u);
        return ca * axis_ + sa * (cu * u_axis_ + su * v_axis_);
    }

    /// D'(u) = sa*(-sin(u)*uAxis + cos(u)*vAxis)
    Vector3 directionDer(double u) const {
        double sa = std::sin(half_angle_);
        double cu = std::cos(u), su = std::sin(u);
        return sa * (-su * u_axis_ + cu * v_axis_);
    }

    /// D''(u) = sa*(-cos(u)*uAxis - sin(u)*vAxis)
    Vector3 directionDer2(double u) const {
        double sa = std::sin(half_angle_);
        double cu = std::cos(u), su = std::sin(u);
        return sa * (-cu * u_axis_ - su * v_axis_);
    }

    /// D^(m)(u): D 对 u 的 m 阶导数（周期 4）
    Vector3 directionDerN(size_t m, double u) const {
        switch (m) {
        case 0: return direction(u);
        case 1: return directionDer(u);
        case 2: return directionDer2(u);
        default: break;
        }
        // D 的三角函数部分周期为 4
        // D^(3) = -D'(u), D^(4) = -D''(u), D^(5) = D'(u), ...
        double sa = std::sin(half_angle_);
        double cu = std::cos(u), su = std::sin(u);
        switch (m % 4) {
        case 0: {
            // D^(4k) = D(u) 的三角部分
            double ca = std::cos(half_angle_);
            return ca * axis_ + sa * (cu * u_axis_ + su * v_axis_);
        }
        case 1: return sa * (-su * u_axis_ + cu * v_axis_);
        case 2: return sa * (-cu * u_axis_ - su * v_axis_);
        case 3: return sa * (su * u_axis_ - cu * v_axis_);
        }
        return Vector3(0.0);
    }
};

} // namespace mulan::geometry
