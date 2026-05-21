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
        , minor_radius_(0.3)
        , u_axis_(1.0, 0.0, 0.0)
        , v_axis_(0.0, 1.0, 0.0) {}

    Torus(Point3 center, double major_r, double minor_r)
        : center_(std::move(center))
        , major_radius_(major_r)
        , minor_radius_(minor_r)
        , u_axis_(1.0, 0.0, 0.0)
        , v_axis_(0.0, 1.0, 0.0) {}

    /// 指定轴的环面
    Torus(Point3 center, double major_r, double minor_r,
          Vector3 u_axis, Vector3 v_axis)
        : center_(std::move(center))
        , major_radius_(major_r)
        , minor_radius_(minor_r)
        , u_axis_(glm::normalize(u_axis))
        , v_axis_(glm::normalize(v_axis)) {}

    const Point3& center() const { return center_; }
    double major_radius() const { return major_radius_; }
    double minor_radius() const { return minor_radius_; }
    const Vector3& u_axis() const { return u_axis_; }
    const Vector3& v_axis() const { return v_axis_; }
    Vector3 normal() const { return glm::cross(u_axis_, v_axis_); }

    // u ∈ [0, 2π), v ∈ [0, 2π)
    // subs(u,v) = center + (R + r*cos(v))*(cos(u)*u_axis + sin(u)*v_axis) + r*sin(v)*n_axis

    Point3 subs(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        Vector3 U = cu * u_axis_ + su * v_axis_;
        Vector3 N = glm::cross(u_axis_, v_axis_);
        return center_ + (major_radius_ + minor_radius_ * cv) * U + minor_radius_ * sv * N;
    }

    Vector3 uder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        Vector3 dU = -su * u_axis_ + cu * v_axis_;
        return (major_radius_ + minor_radius_ * cv) * dU;
    }

    Vector3 vder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        Vector3 U = cu * u_axis_ + su * v_axis_;
        Vector3 N = glm::cross(u_axis_, v_axis_);
        return -minor_radius_ * sv * U + minor_radius_ * cv * N;
    }

    Vector3 uuder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v);
        Vector3 U = cu * u_axis_ + su * v_axis_;
        return -(major_radius_ + minor_radius_ * cv) * U;
    }

    Vector3 uvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        (void)sv;
        Vector3 dU = -su * u_axis_ + cu * v_axis_;
        return -minor_radius_ * cv * dU;
    }

    Vector3 vvder(double u, double v) const override {
        double cu = std::cos(u), su = std::sin(u);
        double cv = std::cos(v), sv = std::sin(v);
        Vector3 U = cu * u_axis_ + su * v_axis_;
        Vector3 N = glm::cross(u_axis_, v_axis_);
        return -minor_radius_ * cv * U - minor_radius_ * sv * N;
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

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        auto c = mat * glm::dvec4(center_, 1.0);
        center_ = Point3(c);
        u_axis_ = Vector3(mat * glm::dvec4(u_axis_, 0.0));
        v_axis_ = Vector3(mat * glm::dvec4(v_axis_, 0.0));
        // 通过轴长平均估算半径缩放
        double su = glm::length(u_axis_);
        double sv = glm::length(v_axis_);
        double s = (su + sv) * 0.5;
        u_axis_ /= su;
        v_axis_ /= sv;
        major_radius_ *= s;
        minor_radius_ *= s;
    }

private:
    Point3 center_;
    double major_radius_;
    double minor_radius_;
    Vector3 u_axis_{1.0, 0.0, 0.0};
    Vector3 v_axis_{0.0, 1.0, 0.0};
};

} // namespace MulanGeo::Geometry
