/**
 * @file ExtrudedCurve.h
 * @brief 挤压曲面 (曲线沿向量挤压成曲面)
 *
 * 基于 truck-geometry::decorators::ExtrudedCurve。
 * subs(u, v) = curve.subs(u) + v * extrusion_vec
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricSurface.h"
#include "../Export.h"
#include <memory>

namespace mulan::geometry {

/// 挤压曲面: 3D 曲线沿向量 v 挤压
template<typename Curve>
class ExtrudedCurve : public ParametricSurface3D {
public:
    ExtrudedCurve() = default;
    ExtrudedCurve(Curve curve, Vector3 extrusion_vec)
        : curve_(std::move(curve))
        , vec_(std::move(extrusion_vec)) {}

    const Curve& generatingCurve() const { return curve_; }
    const Vector3& extrusionVector() const { return vec_; }

    Point3 subs(double u, double v) const override {
        return curve_.subs(u) + vec_ * v;
    }

    Vector3 uder(double u, double v) const override { return curve_.der(u); }
    Vector3 vder(double u, double v) const override { return vec_; }
    Vector3 uuder(double u, double v) const override { return curve_.der2(u); }
    Vector3 uvder(double u, double v) const override { return Vector3(0.0); }
    Vector3 vvder(double u, double v) const override { return Vector3(0.0); }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        if (n == 0) return curve_.derN(m, u);
        if (n == 1 && m == 0) return vec_;
        return Vector3(0.0);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        auto cr = curve_.parameterRange();
        Bound unbounded{BoundKind::Unbounded, 0.0};
        return {cr, {unbounded, unbounded}};
    }

    // --- 变换 ---

    void transformBy(const Matrix4& mat) override {
        vec_ = Vector3(mat * glm::dvec4(vec_, 0.0));
        if constexpr (requires { curve_.transformBy(mat); }) {
            curve_.transformBy(mat);
        }
    }

private:
    Curve curve_;
    Vector3 vec_;
};

} // namespace mulan::Geometry
