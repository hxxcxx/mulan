/**
 * @file OffsetCurve.h
 * @brief 等距偏移曲线
 *
 * 将曲线沿其法线方向偏移固定距离。
 * 2D: offset(t) = curve(t) + distance * normal(t)
 *     其中 normal(t) = rotate90(normalize(der(t)))
 *
 * 3D: 需要提供参考法向量来确定偏移平面。
 *     offset(t) = curve(t) + distance * normalize(der(t) × refNormal) × normalize(der(t))
 *     简化为: offset(t) = curve(t) + distance * side(t)
 *     其中 side(t) = normalize(refNormal × der(t))
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

/// 2D 等距偏移曲线
template<typename Curve>
class OffsetCurve2D : public BoundedCurve<Point2, Vector2> {
public:
    OffsetCurve2D() = default;

    OffsetCurve2D(Curve curve, double distance)
        : curve_(std::move(curve)), distance_(distance) {}

    const Curve& baseCurve() const { return curve_; }
    double offsetDistance() const { return distance_; }
    void setOffsetDistance(double d) { distance_ = d; }

    // offset(t) = curve(t) + d * n(t)
    // n(t) = rotate90(normalize(der(t))) = (-dy, dx) / |der|

    Point2 subs(double t) const override {
        Point2 p = curve_.subs(t);
        Vector2 d = curve_.der(t);
        double len = glm::length(d);
        if (soSmall(len)) return p;
        Vector2 n(-d.y / len, d.x / len);
        return p + distance_ * n;
    }

    Vector2 der(double t) const override {
        Vector2 d = curve_.der(t);
        Vector2 dd = curve_.der2(t);
        double len = glm::length(d);
        if (soSmall(len)) return Vector2(0.0);

        // n(t) = (-d.y, d.x) / |d|
        // n'(t) = (-dd.y, dd.x) / |d| - (-d.y, d.x) * dot(d, dd) / |d|^3
        double len3 = len * len * len;
        double dot_d_dd = d.x * dd.x + d.y * dd.y;
        Vector2 dn(
            -dd.y / len + d.y * dot_d_dd / len3,
             dd.x / len - d.x * dot_d_dd / len3
        );
        return d + distance_ * dn;
    }

    Vector2 der2(double t) const override {
        // 数值差分（解析公式过于复杂）
        double h = 1e-6;
        Vector2 d1 = der(t + h);
        Vector2 d0 = der(t - h);
        return (d1 - d0) / (2.0 * h);
    }

    Vector2 derN(size_t n, double t) const override {
        if (n == 0) return subs(t) - Point2(0.0);
        if (n == 1) return der(t);
        if (n == 2) return der2(t);
        // 高阶数值差分
        double h = 1e-5;
        auto f = [&](double tt) { return derN(n - 1, tt); };
        return (f(t + h) - f(t - h)) / (2.0 * h);
    }

    ParameterRange parameterRange() const override {
        return curve_.parameterRange();
    }

    std::optional<double> period() const override { return curve_.period(); }
    std::pair<double, double> rangeTuple() const override { return curve_.rangeTuple(); }

    std::pair<std::vector<double>, std::vector<Point2>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        return defaultDivision(range, tol);
    }

    void transformBy(const Matrix4& mat) override {
        if constexpr (requires { curve_.transformBy(mat); }) {
            curve_.transformBy(mat);
        }
        // 偏移距离按缩放因子调整
        double s = std::sqrt(std::abs(mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0]));
        distance_ *= s;
    }

private:
    Curve curve_;
    double distance_ = 0.0;

    std::pair<std::vector<double>, std::vector<Point2>>
    defaultDivision(std::pair<double, double> range, double tol) const {
        double t0 = range.first, t1 = range.second;
        size_t n = std::max(size_t(16), static_cast<size_t>((t1 - t0) / tol * 0.1) + 1);
        n = std::min(n, size_t(10000));
        std::vector<double> params;
        std::vector<Point2> points;
        params.reserve(n + 1);
        points.reserve(n + 1);
        for (size_t i = 0; i <= n; ++i) {
            double t = t0 + (t1 - t0) * static_cast<double>(i) / static_cast<double>(n);
            params.push_back(t);
            points.push_back(subs(t));
        }
        return {params, points};
    }
};

/// 3D 等距偏移曲线（需要参考法向量确定偏移平面）
template<typename Curve>
class OffsetCurve3D : public BoundedCurve<Point3, Vector3> {
public:
    OffsetCurve3D() = default;

    /// @param curve 基曲线
    /// @param distance 偏移距离
    /// @param refNormal 参考法向量（确定偏移方向在哪个平面内）
    OffsetCurve3D(Curve curve, double distance, Vector3 refNormal)
        : curve_(std::move(curve))
        , distance_(distance)
        , ref_normal_(glm::normalize(refNormal)) {}

    const Curve& baseCurve() const { return curve_; }
    double offsetDistance() const { return distance_; }
    const Vector3& referenceNormal() const { return ref_normal_; }

    void setOffsetDistance(double d) { distance_ = d; }
    void setReferenceNormal(const Vector3& n) { ref_normal_ = glm::normalize(n); }

    // offset(t) = curve(t) + d * side(t)
    // side(t) = normalize(refNormal × der(t))

    Point3 subs(double t) const override {
        Point3 p = curve_.subs(t);
        Vector3 d = curve_.der(t);
        Vector3 side = glm::cross(ref_normal_, d);
        double len = glm::length(side);
        if (soSmall(len)) return p;
        return p + distance_ * (side / len);
    }

    Vector3 der(double t) const override {
        // 数值差分（3D 偏移的解析导数非常复杂）
        double h = 1e-6;
        Point3 p1 = subs(t + h);
        Point3 p0 = subs(t - h);
        return (p1 - p0) / (2.0 * h);
    }

    Vector3 der2(double t) const override {
        double h = 1e-5;
        Vector3 d1 = der(t + h);
        Vector3 d0 = der(t - h);
        return (d1 - d0) / (2.0 * h);
    }

    Vector3 derN(size_t n, double t) const override {
        if (n == 0) return subs(t) - Point3(0.0);
        if (n == 1) return der(t);
        if (n == 2) return der2(t);
        double h = 1e-5;
        auto f = [&](double tt) { return derN(n - 1, tt); };
        return (f(t + h) - f(t - h)) / (2.0 * h);
    }

    ParameterRange parameterRange() const override {
        return curve_.parameterRange();
    }

    std::optional<double> period() const override { return curve_.period(); }
    std::pair<double, double> rangeTuple() const override { return curve_.rangeTuple(); }

    std::pair<std::vector<double>, std::vector<Point3>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        double t0 = range.first, t1 = range.second;
        size_t n = std::max(size_t(16), static_cast<size_t>((t1 - t0) / tol * 0.1) + 1);
        n = std::min(n, size_t(10000));
        std::vector<double> params;
        std::vector<Point3> points;
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
        if constexpr (requires { curve_.transformBy(mat); }) {
            curve_.transformBy(mat);
        }
        ref_normal_ = glm::normalize(Vector3(mat * glm::dvec4(ref_normal_, 0.0)));
        double s = std::cbrt(std::abs(glm::determinant(glm::dmat3(mat))));
        distance_ *= s;
    }

private:
    Curve curve_;
    double distance_ = 0.0;
    Vector3 ref_normal_{0.0, 0.0, 1.0};
};

} // namespace mulan::geometry
