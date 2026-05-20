/**
 * @file Plane.h
 * @brief 平面
 *
 * 基于 truck-geometry::specifieds::Plane。
 * subs(u, v) = origin + u * u_axis + v * v_axis
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricSurface.h"
#include "../Export.h"

namespace MulanGeo::Geometry {

/// 平面
class GEOMETRY_API Plane : public ParametricSurface3D {
public:
    Plane()
        : origin_(0.0)
        , u_axis_(1.0, 0.0, 0.0)
        , v_axis_(0.0, 1.0, 0.0) {}

    Plane(Point3 origin, Vector3 u_axis, Vector3 v_axis)
        : origin_(std::move(origin))
        , u_axis_(std::move(u_axis))
        , v_axis_(std::move(v_axis)) {}

    const Point3& origin() const { return origin_; }
    const Vector3& u_axis() const { return u_axis_; }
    const Vector3& v_axis() const { return v_axis_; }
    Vector3 normal() const { return glm::normalize(glm::cross(u_axis_, v_axis_)); }

    // --- ParametricSurface ---

    Point3 subs(double u, double v) const override {
        return origin_ + u_axis_ * u + v_axis_ * v;
    }

    Vector3 uder(double u, double v) const override { return u_axis_; }
    Vector3 vder(double u, double v) const override { return v_axis_; }
    Vector3 uuder(double u, double v) const override { return Vector3(0.0); }
    Vector3 uvder(double u, double v) const override { return Vector3(0.0); }
    Vector3 vvder(double u, double v) const override { return Vector3(0.0); }
    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (m == 1 && n == 0) return u_axis_;
        if (m == 0 && n == 1) return v_axis_;
        return Vector3(0.0);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        Bound unbounded{BoundKind::Unbounded, 0.0};
        return {{unbounded, unbounded}, {unbounded, unbounded}};
    }

    void transformBy(const Matrix4& mat) override {
        auto o = mat * glm::dvec4(origin_, 1.0);
        origin_ = Point3(o);
        u_axis_ = Vector3(mat * glm::dvec4(u_axis_, 0.0));
        v_axis_ = Vector3(mat * glm::dvec4(v_axis_, 0.0));
    }

private:
    Point3 origin_;
    Vector3 u_axis_;
    Vector3 v_axis_;
};

} // namespace MulanGeo::Geometry
