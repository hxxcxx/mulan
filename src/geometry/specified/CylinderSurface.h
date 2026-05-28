/**
 * @file CylinderSurface.h
 * @brief 圆柱面
 *
 * 参数化: subs(u, v) = origin + v * axis + R * (cos(u) * uAxis + sin(u) * vAxis)
 *
 * 其中 uAxis, vAxis 是与 axis 正交的单位向量。
 * u ∈ [0, 2π), v ∈ (-∞, +∞)
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

/// 圆柱面
class GEOMETRY_API CylinderSurface : public ParametricSurface3D {
public:
    CylinderSurface()
        : origin_(0.0)
        , axis_(0.0, 0.0, 1.0)
        , u_axis_(1.0, 0.0, 0.0)
        , v_axis_(0.0, 1.0, 0.0)
        , radius_(1.0) {}

    CylinderSurface(Point3 origin, Vector3 axis, double radius)
        : origin_(std::move(origin))
        , axis_(glm::normalize(axis))
        , radius_(radius)
    {
        computeOrthogonalBasis();
    }

    /// 完整构造（指定正交基）
    CylinderSurface(Point3 origin, Vector3 axis, double radius,
                    Vector3 uAxis, Vector3 vAxis)
        : origin_(std::move(origin))
        , axis_(glm::normalize(axis))
        , u_axis_(glm::normalize(uAxis))
        , v_axis_(glm::normalize(vAxis))
        , radius_(radius) {}

    const Point3& origin() const { return origin_; }
    const Vector3& axis() const { return axis_; }
    double radius() const { return radius_; }
    const Vector3& uAxis() const { return u_axis_; }
    const Vector3& vAxis() const { return v_axis_; }

    // --- ParametricSurface ---
    // subs(u, v) = origin + v*axis + R*(cos(u)*uAxis + sin(u)*vAxis)
    //
    // 关于 u 的导数（对 cos/sin 求导，周期 4）:
    //   ∂/∂u   = R*(-sin(u)*uAxis + cos(u)*vAxis)
    //   ∂²/∂u² = R*(-cos(u)*uAxis - sin(u)*vAxis)
    //   ∂³/∂u³ = R*(sin(u)*uAxis - cos(u)*vAxis) = -∂/∂u
    //   ∂⁴/∂u⁴ = ∂/∂u (周期 4)
    //
    // 关于 v 的导数:
    //   ∂/∂v   = axis (常数)
    //   ∂ⁿ/∂vⁿ = 0 (n >= 2)
    //
    // 混合导数:
    //   ∂²/∂u∂v = 0

    Point3 subs(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        return origin_ + v * axis_ + radius_ * (cu * u_axis_ + su * v_axis_);
    }

    Vector3 uder(double u, double /*v*/) const override {
        double cu = std::cos(u), su = std::sin(u);
        return radius_ * (-su * u_axis_ + cu * v_axis_);
    }

    Vector3 vder(double /*u*/, double /*v*/) const override {
        return axis_;
    }

    Vector3 uuder(double u, double /*v*/) const override {
        double cu = std::cos(u), su = std::sin(u);
        return radius_ * (-cu * u_axis_ - su * v_axis_);
    }

    Vector3 uvder(double /*u*/, double /*v*/) const override {
        return Vector3(0.0);
    }

    Vector3 vvder(double /*u*/, double /*v*/) const override {
        return Vector3(0.0);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (n >= 2) return Vector3(0.0);  // v 方向二阶以上为零
        if (n == 1 && m >= 1) return Vector3(0.0);  // 混合导数为零

        // n == 0: 纯 u 方向导数（周期 4）
        double cu = std::cos(u), su = std::sin(u);
        switch (m % 4) {
        case 0: return subs(u, v) - Point3(0.0);
        case 1: return radius_ * (-su * u_axis_ + cu * v_axis_);
        case 2: return radius_ * (-cu * u_axis_ - su * v_axis_);
        case 3: return radius_ * (su * u_axis_ - cu * v_axis_);
        }
        return Vector3(0.0);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        Bound unbounded{BoundKind::Unbounded, 0.0};
        return {
            {Bound{BoundKind::Included, 0.0}, Bound{BoundKind::Excluded, TWO_PI}},
            {unbounded, unbounded}
        };
    }

    std::optional<double> uPeriod() const override { return TWO_PI; }

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        auto tp = [&](const Point3& p) { return Point3(mat * glm::dvec4(p, 1.0)); };
        auto tv = [&](const Vector3& v) { return Vector3(mat * glm::dvec4(v, 0.0)); };

        origin_ = tp(origin_);
        Vector3 newAxis = tv(axis_);
        Vector3 newU = tv(radius_ * u_axis_);
        Vector3 newV = tv(radius_ * v_axis_);

        double lu = glm::length(newU);
        double lv = glm::length(newV);
        radius_ = (lu + lv) * 0.5;

        axis_ = glm::normalize(newAxis);
        if (!soSmall(lu)) u_axis_ = newU / lu;
        if (!soSmall(lv)) v_axis_ = newV / lv;
    }

private:
    Point3 origin_;
    Vector3 axis_;
    Vector3 u_axis_;
    Vector3 v_axis_;
    double radius_;

    void computeOrthogonalBasis() {
        if (std::abs(axis_.x) < 0.9 && std::abs(axis_.y) < 0.9) {
            u_axis_ = glm::normalize(glm::cross(axis_, Vector3(0.0, 0.0, 1.0)));
        } else {
            u_axis_ = glm::normalize(glm::cross(axis_, Vector3(1.0, 0.0, 0.0)));
        }
        v_axis_ = glm::cross(axis_, u_axis_);
    }
};

} // namespace mulan::geometry
