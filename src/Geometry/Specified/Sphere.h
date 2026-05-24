/**
 * @file Sphere.h
 * @brief 球面
 *
 * 基于 truck-geometry::specifieds::Sphere。
 * 参数化: subs(u,v) = center + R*(cos(u)*cos(v), sin(u)*cos(v), sin(v))
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

namespace MulanGeo::geometry {

/// 球面
class GEOMETRY_API Sphere : public ParametricSurface3D {
public:
    Sphere()
        : center_(0.0)
        , radius_(1.0) {}

    Sphere(Point3 center, double radius)
        : center_(std::move(center))
        , radius_(radius) {}

    const Point3& center() const { return center_; }
    double radius() const { return radius_; }

    // --- ParametricSurface ---
    // u ∈ [0, 2π), v ∈ [-π/2, π/2]

    Point3 subs(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        return center_ + radius_ * Vector3(cu * cv, su * cv, sv);
    }

    Vector3 uder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        return radius_ * Vector3(-su * cv, cu * cv, 0.0);
    }

    Vector3 vder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        return radius_ * Vector3(-cu * sv, -su * sv, cv);
    }

    Vector3 uuder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        return radius_ * Vector3(-cu * cv, -su * cv, 0.0);
    }

    Vector3 uvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        return radius_ * Vector3(su * sv, -cu * sv, 0.0);
    }

    Vector3 vvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        return radius_ * Vector3(-cu * cv, -su * cv, -sv);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        // 使用数值差分作为 fallback
        double eps = 1e-6;
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (m == 1 && n == 0) return uder(u, v);
        if (m == 0 && n == 1) return vder(u, v);
        if (m == 2 && n == 0) return uuder(u, v);
        if (m == 1 && n == 1) return uvder(u, v);
        if (m == 0 && n == 2) return vvder(u, v);
        // 高阶
        return Vector3(0.0);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        return {
            {Bound{BoundKind::Included, 0.0}, Bound{BoundKind::Excluded, TWO_PI}},
            {Bound{BoundKind::Included, -PI / 2.0}, Bound{BoundKind::Included, PI / 2.0}}
        };
    }

    std::optional<double> uPeriod() const override { return TWO_PI; }

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        auto c = mat * glm::dvec4(center_, 1.0);
        center_ = Point3(c);
        // 通过矩阵行列式提取均匀缩放因子
        double s = std::cbrt(std::abs(glm::determinant(glm::dmat3(mat))));
        radius_ *= s;
    }

private:
    Point3 center_;
    double radius_;
};

} // namespace MulanGeo::Geometry
