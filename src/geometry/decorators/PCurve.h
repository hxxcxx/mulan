/**
 * @file PCurve.h
 * @brief 参数曲线映射 (2D曲线映射到3D曲面)
 *
 * 基于 truck-geometry::decorators::PCurve。
 * PCurve 的求值: 将 2D 曲线映射到 3D 曲面上。
 * subs(t) = surface.subs(curve.subs(t))
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"

namespace mulan::geometry {

/// 参数曲线: 2D 曲线在 3D 曲面上的映射
template<typename Curve2D, typename Surface>
class PCurve : public ParametricCurve<Point3, Vector3> {
public:
    PCurve() = default;
    PCurve(Curve2D curve, Surface surface)
        : curve_(std::move(curve))
        , surface_(std::move(surface)) {}

    const Curve2D& parameterCurve() const { return curve_; }
    const Surface& hostSurface() const { return surface_; }

    Point3 subs(double t) const override {
        auto uv = curve_.subs(t);
        return surface_.subs(uv.x, uv.y);
    }

    Vector3 der(double t) const override {
        auto uv = curve_.subs(t);
        auto duv = curve_.der(t);
        return surface_.uder(uv.x, uv.y) * duv.x +
               surface_.vder(uv.x, uv.y) * duv.y;
    }

    Vector3 der2(double t) const override {
        auto uv = curve_.subs(t);
        auto d1 = curve_.der(t);
        auto d2 = curve_.der2(t);
        auto Su = surface_.uder(uv.x, uv.y);
        auto Sv = surface_.vder(uv.x, uv.y);
        auto Suu = surface_.uuder(uv.x, uv.y);
        auto Suv = surface_.uvder(uv.x, uv.y);
        auto Svv = surface_.vvder(uv.x, uv.y);
        return Suu * d1.x * d1.x + 2.0 * Suv * d1.x * d1.y +
               Svv * d1.y * d1.y + Su * d2.x + Sv * d2.y;
    }

    Vector3 derN(size_t n, double t) const override {
        // 高阶导数用数值差分
        double eps = 1e-6;
        if (n == 0) return subs(t) - Point3(0.0);
        auto d0 = derN(n - 1, t - eps);
        auto d1 = derN(n - 1, t + eps);
        return (d1 - d0) / (2.0 * eps);
    }

    ParameterRange parameterRange() const override {
        return curve_.parameterRange();
    }

    std::optional<double> period() const override { return curve_.period(); }

private:
    Curve2D curve_;
    Surface surface_;
};

} // namespace mulan::Geometry
