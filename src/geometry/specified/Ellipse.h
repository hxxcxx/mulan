/**
 * @file Ellipse.h
 * @brief 椭圆 (2D/3D)
 *
 * 2D: subs(t) = center + (a*cos(t), b*sin(t))
 * 3D: subs(t) = center + a*cos(t)*u_axis + b*sin(t)*v_axis
 *
 * 其中 u_axis, v_axis 是单位正交向量，a, b 是半轴长度。
 *
 * @author hxxcxx
 * @date 2026-05-28
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"
#include <cmath>

namespace mulan::geometry {

/// 椭圆
/// @tparam P 点类型 (Point2/Point3)
template<typename P>
class Ellipse : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    Ellipse() = default;

    /// 2D 椭圆: 圆心 + 两个半轴长度
    Ellipse(Point2 center, double semiMajor, double semiMinor)
        requires std::same_as<P, Point2>
        : center_(center), a_(semiMajor), b_(semiMinor) {}

    /// 3D 椭圆: 圆心 + 两个半轴向量（方向即轴方向，长度即半轴长）
    Ellipse(Point3 center, Vector3 majorAxis, Vector3 minorAxis)
        requires std::same_as<P, Point3>
        : center_(center)
        , a_(glm::length(majorAxis))
        , b_(glm::length(minorAxis))
        , u_axis_(glm::normalize(majorAxis))
        , v_axis_(glm::normalize(minorAxis)) {}

    const P& center() const { return center_; }
    double semiMajor() const { return a_; }
    double semiMinor() const { return b_; }

    /// 3D 专用: 获取主轴方向
    const Vector3& majorAxis() const
        requires std::same_as<P, Point3>
    { return u_axis_; }

    const Vector3& minorAxis() const
        requires std::same_as<P, Point3>
    { return v_axis_; }

    /// 3D 专用: 法线方向
    Vector3 normal() const
        requires std::same_as<P, Point3>
    { return glm::cross(u_axis_, v_axis_); }

    /// 离心率 e = sqrt(1 - (b/a)^2), 假设 a >= b
    double eccentricity() const {
        double aa = std::max(a_, b_);
        double bb = std::min(a_, b_);
        if (soSmall(aa)) return 0.0;
        return std::sqrt(1.0 - (bb * bb) / (aa * aa));
    }

    // --- ParametricCurve ---

    P subs(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return center_ + Point2(a_ * ct, b_ * st);
        } else {
            return center_ + a_ * ct * u_axis_ + b_ * st * v_axis_;
        }
    }

    Diff der(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(-a_ * st, b_ * ct);
        } else {
            return -a_ * st * u_axis_ + b_ * ct * v_axis_;
        }
    }

    Diff der2(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(-a_ * ct, -b_ * st);
        } else {
            return -a_ * ct * u_axis_ - b_ * st * v_axis_;
        }
    }

    Diff derN(size_t n, double t) const override {
        // 椭圆导数周期为 4: f, f', f'', -f', -f, ...
        // derN(0) = subs, derN(1) = der, derN(2) = der2 = -subs(相对中心)
        // derN(3) = -der, derN(4) = -subs(相对中心) = derN(0) - center ...
        // 更精确: 对 cos/sin 参数化，n 阶导 = 旋转 n*π/2
        switch (n % 4) {
        case 0: return subs(t) - P(0.0);
        case 1: return der(t);
        case 2: return der2(t);
        case 3: return -der(t);
        }
        return Diff(0.0); // unreachable
    }

    ParameterRange parameterRange() const override {
        return {{BoundKind::Included, 0.0}, {BoundKind::Excluded, TWO_PI}};
    }

    std::optional<double> period() const override { return TWO_PI; }
    std::pair<double, double> rangeTuple() const override { return {0.0, TWO_PI}; }

    void invert() {
        if constexpr (std::same_as<P, Point3>) { v_axis_ = -v_axis_; }
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<P>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        double t0 = range.first, t1 = range.second;
        // 估算弧长: 使用 Ramanujan 近似
        double aa = std::max(a_, b_), bb = std::min(a_, b_);
        double h = (aa - bb) * (aa - bb) / ((aa + bb) * (aa + bb));
        double perimeter = PI * (aa + bb) * (1.0 + 3.0 * h / (10.0 + std::sqrt(4.0 - 3.0 * h)));
        double fraction = (t1 - t0) / TWO_PI;
        double arcLen = perimeter * fraction;
        size_t n = std::max(size_t(8), static_cast<size_t>(arcLen / tol) + 1);
        n = std::min(n, size_t(10000)); // 安全上限

        std::vector<double> params;
        std::vector<P> points;
        params.reserve(n + 1);
        points.reserve(n + 1);
        for (size_t i = 0; i <= n; ++i) {
            double t = t0 + (t1 - t0) * static_cast<double>(i) / static_cast<double>(n);
            params.push_back(t);
            points.push_back(subs(t));
        }
        return {params, points};
    }

    void transformBy(const Matrix4& mat) override {
        if constexpr (std::same_as<P, Point2>) {
            auto v = mat * glm::dvec4(center_, 0.0, 1.0);
            center_ = Point2(v);
            // 2D 变换后需要重新计算半轴（简化处理：只处理均匀缩放）
            double s = std::sqrt(std::abs(mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0]));
            a_ *= s;
            b_ *= s;
        } else {
            auto tp = [&](const Point3& p) -> Point3 {
                return Point3(mat * glm::dvec4(p, 1.0));
            };
            auto tv = [&](const Vector3& v) -> Vector3 {
                return Vector3(mat * glm::dvec4(v, 0.0));
            };
            center_ = tp(center_);
            Vector3 newMajor = tv(a_ * u_axis_);
            Vector3 newMinor = tv(b_ * v_axis_);
            a_ = glm::length(newMajor);
            b_ = glm::length(newMinor);
            if (!soSmall(a_)) u_axis_ = newMajor / a_;
            if (!soSmall(b_)) v_axis_ = newMinor / b_;
        }
    }

private:
    P center_{0.0};
    double a_ = 1.0;
    double b_ = 0.5;
    // 3D 专用: 正交轴方向（单位向量）
    Vector3 u_axis_{1.0, 0.0, 0.0};
    Vector3 v_axis_{0.0, 1.0, 0.0};
};

} // namespace mulan::geometry
