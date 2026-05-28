/**
 * @file Parabola.h
 * @brief 抛物线 (2D/3D)
 *
 * 2D: subs(t) = vertex + (t, t^2 / (4*focalLength))
 * 3D: subs(t) = vertex + t*u_axis + (t^2 / (4*focalLength))*v_axis
 *
 * 其中 focalLength 是焦距（顶点到焦点的距离）。
 * 标准形式: y = x^2 / (4f)，焦点在 (0, f)。
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

/// 抛物线
/// @tparam P 点类型 (Point2/Point3)
template<typename P>
class Parabola : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    Parabola() = default;

    /// 2D 抛物线: 顶点 + 焦距
    Parabola(Point2 vertex, double focalLength)
        requires std::same_as<P, Point2>
        : vertex_(vertex), focal_(focalLength) {}

    /// 3D 抛物线: 顶点 + 焦距 + 两个正交轴方向
    /// @param uAxis 切线方向（抛物线开口方向的垂直方向）
    /// @param vAxis 对称轴方向（抛物线开口方向）
    Parabola(Point3 vertex, double focalLength, Vector3 uAxis, Vector3 vAxis)
        requires std::same_as<P, Point3>
        : vertex_(vertex)
        , focal_(focalLength)
        , u_axis_(glm::normalize(uAxis))
        , v_axis_(glm::normalize(vAxis)) {}

    const P& vertex() const { return vertex_; }
    double focalLength() const { return focal_; }

    /// 焦点位置
    P focus() const {
        if constexpr (std::same_as<P, Point2>) {
            return vertex_ + Point2(0.0, focal_);
        } else {
            return vertex_ + focal_ * v_axis_;
        }
    }

    // --- ParametricCurve ---
    // subs(t)  = vertex + t*u + (t^2/(4f))*v
    // der(t)   = u + (t/(2f))*v
    // der2(t)  = (1/(2f))*v
    // derN(n>=3) = 0

    P subs(double t) const override {
        double coeff = (soSmall(focal_)) ? 0.0 : t * t / (4.0 * focal_);
        if constexpr (std::same_as<P, Point2>) {
            return vertex_ + Point2(t, coeff);
        } else {
            return vertex_ + t * u_axis_ + coeff * v_axis_;
        }
    }

    Diff der(double t) const override {
        double coeff = (soSmall(focal_)) ? 0.0 : t / (2.0 * focal_);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(1.0, coeff);
        } else {
            return u_axis_ + coeff * v_axis_;
        }
    }

    Diff der2(double /*t*/) const override {
        double coeff = (soSmall(focal_)) ? 0.0 : 1.0 / (2.0 * focal_);
        if constexpr (std::same_as<P, Point2>) {
            return Vector2(0.0, coeff);
        } else {
            return coeff * v_axis_;
        }
    }

    Diff derN(size_t n, double t) const override {
        if (n == 0) return subs(t) - P(0.0);
        if (n == 1) return der(t);
        if (n == 2) return der2(t);
        return Diff(0.0); // 抛物线三阶以上导数为零
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
        if constexpr (std::same_as<P, Point3>) { u_axis_ = -u_axis_; }
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<P>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        double t0 = range.first, t1 = range.second;
        // 抛物线在 |t| 大时曲率变化快，需要足够采样
        size_t n = std::max(size_t(16), static_cast<size_t>((t1 - t0) / tol * 0.05) + 1);
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
            auto v = mat * glm::dvec4(vertex_, 0.0, 1.0);
            vertex_ = Point2(v);
            double s = std::sqrt(std::abs(mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0]));
            focal_ *= s;
        } else {
            auto tp = [&](const Point3& p) -> Point3 {
                return Point3(mat * glm::dvec4(p, 1.0));
            };
            auto tv = [&](const Vector3& v) -> Vector3 {
                return Vector3(mat * glm::dvec4(v, 0.0));
            };
            vertex_ = tp(vertex_);
            Vector3 newU = tv(u_axis_);
            Vector3 newV = tv(v_axis_);
            double lu = glm::length(newU);
            double lv = glm::length(newV);
            if (!soSmall(lu)) u_axis_ = newU / lu;
            if (!soSmall(lv)) v_axis_ = newV / lv;
            // 焦距按平均缩放
            focal_ *= (lu + lv) * 0.5;
        }
    }

private:
    P vertex_{0.0};
    double focal_ = 1.0;
    Vector3 u_axis_{1.0, 0.0, 0.0};
    Vector3 v_axis_{0.0, 1.0, 0.0};
    double range_limit_ = 10.0;
};

} // namespace mulan::geometry
