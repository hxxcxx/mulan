/**
 * @file Circle.h
 * @brief 圆 (2D/3D)
 *
 * 2D: subs(t) = center + r*(cos t, sin t)
 * 3D: subs(t) = center + r*(cos t * u + sin t * v)
 *
 * 基于 truck-geometry::specifieds::UnitCircle。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"
#include <cmath>

namespace mulan::geometry {

/// 通用圆
/// @tparam P 点类型 (Point2/Point3)
template<typename P>
class Circle : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    Circle() = default;

    /// 2D 圆: 圆心 + 半径
    Circle(Point2 center, double radius)
        requires std::same_as<P, Point2>
        : center_(center), radius_(radius) {}

    /// 3D 圆: 圆心 + 半径 + 两个正交轴
    Circle(Point3 center, double radius, Vector3 u_axis, Vector3 v_axis)
        requires std::same_as<P, Point3>
        : center_(center), radius_(radius), u_axis_(u_axis), v_axis_(v_axis) {}

    // --- 工厂 ---

    /// 从法线构造 3D 圆 (自动计算正交基)
    static Circle<Point3> from_normal(Point3 center, double radius, Vector3 normal) {
        // 构造正交基: u ⊥ normal, v = normal × u
        Vector3 u, v;
        if (std::abs(normal.x) < 0.9 && std::abs(normal.y) < 0.9) {
            u = glm::normalize(glm::cross(normal, Vector3(0, 0, 1)));
        } else {
            u = glm::normalize(glm::cross(normal, Vector3(1, 0, 0)));
        }
        v = glm::cross(normal, u);
        return Circle<Point3>(center, radius, u, v);
    }

    const P& center() const { return center_; }
    double radius() const { return radius_; }

    // --- ParametricCurve ---

    P subs(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return center_ + radius_ * Point2(ct, st);
        } else {
            return center_ + radius_ * (u_axis_ * ct + v_axis_ * st);
        }
    }

    Diff der(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return radius_ * Vector2(-st, ct);
        } else {
            return radius_ * (-u_axis_ * st + v_axis_ * ct);
        }
    }

    Diff der2(double t) const override {
        double ct = std::cos(t), st = std::sin(t);
        if constexpr (std::same_as<P, Point2>) {
            return radius_ * Vector2(-ct, -st);
        } else {
            return radius_ * (-u_axis_ * ct - v_axis_ * st);
        }
    }

    Diff derN(size_t n, double t) const override {
        if (n == 0) return subs(t) - P(0.0);
        // 第 n 阶导数 = radius * 旋转 n*π/2 的方向向量
        double angle = t + static_cast<double>(n) * 1.5707963267948966; // π/2
        double ct = std::cos(angle), st = std::sin(angle);
        if constexpr (std::same_as<P, Point2>) {
            return radius_ * Vector2(ct, st);
        } else {
            return radius_ * (u_axis_ * ct + v_axis_ * st);
        }
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
        double arc_len = radius_ * (t1 - t0);
        size_t n = std::max(size_t(4), static_cast<size_t>(arc_len / tol) + 1);
        std::vector<double> params;
        std::vector<P> points;
        for (size_t i = 0; i <= n; ++i) {
            double t = t0 + (t1 - t0) * i / n;
            params.push_back(t);
            points.push_back(subs(t));
        }
        return {params, points};
    }

    void transformBy(const Matrix4& mat) override {
        auto t = [&](const Point3& p) -> Point3 {
            auto v = mat * glm::dvec4(p, 1.0);
            return Point3(v);
        };
        if constexpr (std::same_as<P, Point2>) {
            auto v = mat * glm::dvec4(center_, 0.0, 1.0);
            center_ = Point2(v);
        } else {
            center_ = t(center_);
            u_axis_ = Vector3(mat * glm::dvec4(u_axis_, 0.0));
            v_axis_ = Vector3(mat * glm::dvec4(v_axis_, 0.0));
        }
    }

private:
    P center_{0.0};
    double radius_ = 1.0;
    // 3D 专用: 正交基向量 (Circle<Point2> 不使用)
    Vector3 u_axis_{1.0, 0.0, 0.0};
    Vector3 v_axis_{0.0, 1.0, 0.0};
};

} // namespace mulan::Geometry
