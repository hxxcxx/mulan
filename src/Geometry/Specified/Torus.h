/**
 * @file Torus.h
 * @brief 环面
 *
 * 基于 truck-geometry::specifieds::Torus。
 * 参数化: subs(u,v) = center + (R + r*cos(v))*(cos(u), sin(u), 0) + r*sin(v)*(0, 0, 1)
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricSurface.h"
#include "../Export.h"
#include <cmath>

namespace MulanGeo::Geometry {

/// 环面
class GEOMETRY_API Torus : public ParametricSurface3D {
public:
    Torus()
        : center_(0.0)
        , major_radius_(1.0)
        , minor_radius_(0.3) {}

    Torus(Point3 center, double major_r, double minor_r)
        : center_(std::move(center))
        , major_radius_(major_r)
        , minor_radius_(minor_r) {}

    const Point3& center() const { return center_; }
    double major_radius() const { return major_radius_; }
    double minor_radius() const { return minor_radius_; }

    // u ∈ [0, 2π), v ∈ [0, 2π)

    Point3 subs(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        double r = minor_radius_;
        double R = major_radius_;
        return center_ + Vector3((R + r * cv) * cu, (R + r * cv) * su, r * sv);
    }

    Vector3 uder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        double r = minor_radius_;
        double R = major_radius_;
        return Vector3(-(R + r * cv) * su, (R + r * cv) * cu, 0.0);
    }

    Vector3 vder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        double r = minor_radius_;
        return Vector3(-r * cv * cu, -r * cv * su, r * sv);
    }

    Vector3 uuder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        double r = minor_radius_;
        double R = major_radius_;
        return Vector3(-(R + r * cv) * cu, -(R + r * cv) * su, 0.0);
    }

    Vector3 uvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        double r = minor_radius_;
        return Vector3(r * cv * su, -r * cv * cu, 0.0);
    }

    Vector3 vvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        double r = minor_radius_;
        return Vector3(r * sv * cu, r * sv * su, -r * cv);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (m == 1 && n == 0) return uder(u, v);
        if (m == 0 && n == 1) return vder(u, v);
        if (m == 2 && n == 0) return uuder(u, v);
        if (m == 1 && n == 1) return uvder(u, v);
        if (m == 0 && n == 2) return vvder(u, v);
        return Vector3(0.0);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        Bound u0{BoundKind::Included, 0.0};
        Bound u1{BoundKind::Excluded, 2.0 * M_PI};
        Bound v0{BoundKind::Included, 0.0};
        Bound v1{BoundKind::Excluded, 2.0 * M_PI};
        return {{u0, u1}, {v0, v1}};
    }

    std::optional<double> uPeriod() const override { return 2.0 * M_PI; }
    std::optional<double> vPeriod() const override { return 2.0 * M_PI; }

private:
    Point3 center_;
    double major_radius_;
    double minor_radius_;
};

} // namespace MulanGeo::Geometry
