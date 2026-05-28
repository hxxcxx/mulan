/**
 * @file Hyperbola.h
 * @brief 双曲线 (2D/3D)
 *
 * 2D: subs(t) = center + (a*cosh(t), b*sinh(t))
 * 3D: subs(t) = center + a*cosh(t)*u_axis + b*sinh(t)*v_axis
 *
 * 双曲线是非周期开放曲线，参数范围 (-∞, +∞)。
 * 实际使用时通常截取有限参数范围。
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

/// 双曲线
/// @tparam P 点类型 (Point2/Point3)
template<typename P>
class Hyperbola : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    Hyperbola() = default;

    /// 2D 双曲线: 中心 + 两个半轴
    Hyperbola(Point2 center, double semiMajor, double semiMinor)
        requires std::same_as<P, Point2>
        : center_(center), a_(semiMajor), b_(semiMinor) {}

    /// 3D 双曲线: 中心 + 两个半轴向量
    Hyperbola(Point3 center, Vector3 majorAxis, Vector3 minorAxis)
        requires std::same_as<P, Point3>
        : center_(center)
        , a_(glm::length(majorAxis))
        , b_(glm::length(minorAxis))
        , u_axis_(glm::normalize(majorAxis))
        , v_axis_(glm::normalize(minorAxis)) {}

    const P& center() const { return center_; }
    double semiMajor() const { return a_; }
    double semiMinor() const { return b_; }

    /// 离心率 e = sqrt(1 + (b/a)^2)
    double eccentricity() const {
        if (soSmall(a_)) return 0.0;
        return std::sqrt(1.0 + (b_ * b_) / (a_ * a_));
    }

    // --- ParametricCurve ---
    // subs(t) = center + a*cosh(t)*u + b*sinh(t)*v
    // der(t)  = a*sinh(t)*u + b*cosh(t)*v
    // der2(t) = a*cosh(t)*u + b*sinh(t)*v = subs(t) - center
    // der3(t) = a*sinh(t)*u + b*cosh(t)*v = der(t)
    // 周期为 2: derN(n) = derN(n % 2 == 0 ? 0 or 2 : 1 or 3)

    P subs(double t) const override {
        double cht = std::cosh(t), sht = std::sinh(t);
        if constexpr (std::same_as<P, Point2>) {
            return center_ + Point2(a_ * cht, b_ * sht);
        } else {
            return center_ + a_ * cht * u_axis_ + b_ * sht * v_axis_;
        }
    }

    Diff der(double t) const override {
        double cht = std::cosh(t), sht = std::sinh(t);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(a_ * sht, b_ * cht);
        } else {
            return a_ * sht * u_axis_ + b_ * cht * v_axis_;
        }
    }

    Diff der2(double t) const override {
        // der2 = a*cosh(t)*u + b*sinh(t)*v = subs(t) - center
        double cht = std::cosh(t), sht = std::sinh(t);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(a_ * cht, b_ * sht);
        } else {
            return a_ * cht * u_axis_ + b_ * sht * v_axis_;
        }
    }

    Diff derN(size_t n, double t) const override {
        // cosh 和 sinh 的导数交替:
        // d/dt cosh = sinh, d/dt sinh = cosh
        // 所以 derN(n) 的模式是:
        //   n even: a*cosh(t)*u + b*sinh(t)*v  (= der2 = subs - center)
        //   n odd:  a*sinh(t)*u + b*cosh(t)*v  (= der)
        if (n == 0) return subs(t) - P(0.0);
        if (n % 2 == 1) return der(t);
        return der2(t);
    }

    ParameterRange parameterRange() const override {
        return {{BoundKind::Unbounded, 0.0}, {BoundKind::Unbounded, 0.0}};
    }

    std::optional<double> period() const override { return std::nullopt; }

    /// 默认截取范围 [-T, T]
    std::pair<double, double> rangeTuple() const override {
        return {-range_limit_, range_limit_};
    }

    /// 设置截取范围
    void setRangeLimit(double limit) { range_limit_ = std::abs(limit); }
    double rangeLimit() const { return range_limit_; }

    void invert() {
        if constexpr (std::same_as<P, Point3>) { v_axis_ = -v_axis_; }
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<P>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        double t0 = range.first, t1 = range.second;
        // 双曲线在 |t| 大时变化剧烈，需要自适应
        size_t n = std::max(size_t(16), static_cast<size_t>((t1 - t0) / tol * 0.1) + 1);
        n = std::min(n, size_t(10000));

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
    Vector3 u_axis_{1.0, 0.0, 0.0};
    Vector3 v_axis_{0.0, 1.0, 0.0};
    double range_limit_ = 5.0; // 默认截取范围
};

} // namespace mulan::geometry
