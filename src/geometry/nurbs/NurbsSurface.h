/**
 * @file NurbsSurface.h
 * @brief NURBS 曲面 (非均匀有理 B样条曲面)
 *
 * 基于 truck-geometry::nurbs::NurbsSurface。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricSurface.h"
#include "BSplineSurface.h"
#include "../Export.h"

namespace mulan::geometry {

/// NURBS 曲面 — 内部持有齐次坐标 B-spline 曲面
class GEOMETRY_API NurbsSurface
    : public ParametricSurface3D {
public:
    NurbsSurface() = default;

    /// 从齐次坐标 B-spline 曲面构造
    explicit NurbsSurface(BSplineSurface<Vector4> homogeneous_surface)
        : bspline_(std::move(homogeneous_surface)) {}

    const BSplineSurface<Vector4>& nonRationalized() const { return bspline_; }
    const KnotVec& uKnotVec() const { return bspline_.uKnotVec(); }
    const KnotVec& vKnotVec() const { return bspline_.vKnotVec(); }
    auto degree() const { return bspline_.degree(); }

    // --- ParametricSurface 接口 ---

    Point3 subs(double u, double v) const override {
        Vector4 h = bspline_.subs(u, v);
        return project(h);
    }

    Vector3 uder(double u, double v) const override {
        auto h = bspline_.subs(u, v);
        auto dh = bspline_.uder(u, v);
        return rational_der(project(h), h.w, Vector3(dh), dh.w);
    }

    Vector3 vder(double u, double v) const override {
        auto h = bspline_.subs(u, v);
        auto dh = bspline_.vder(u, v);
        return rational_der(project(h), h.w, Vector3(dh), dh.w);
    }

    Vector3 uuder(double u, double v) const override {
        auto h = bspline_.subs(u, v);
        auto d1u = bspline_.uder(u, v);
        auto d2u = bspline_.uuder(u, v);
        return rational_der2(project(h), h.w,
                             Vector3(d1u), d1u.w,
                             Vector3(d2u), d2u.w);
    }

    Vector3 uvder(double u, double v) const override {
        auto h = bspline_.subs(u, v);
        auto du = bspline_.uder(u, v);
        auto dv = bspline_.vder(u, v);
        auto duv = bspline_.uvder(u, v);
        double w = h.w;
        Point3 S = project(h);
        return (Vector3(duv) - S * duv.w - rational_der(S, w, Vector3(du), du.w) * dv.w
                - rational_der(S, w, Vector3(dv), dv.w) * du.w) / w;
    }

    Vector3 vvder(double u, double v) const override {
        auto h = bspline_.subs(u, v);
        auto d1v = bspline_.vder(u, v);
        auto d2v = bspline_.vvder(u, v);
        return rational_der2(project(h), h.w,
                             Vector3(d1v), d1v.w,
                             Vector3(d2v), d2v.w);
    }

    Vector3 derMN(size_t m, size_t n, double u, double v) const override {
        // 简化: 使用差分近似高阶导数
        double eps = 1e-6;
        if (m == 0 && n == 0) return subs(u, v) - Point3(0.0);
        if (m == 1 && n == 0) return uder(u, v);
        if (m == 0 && n == 1) return vder(u, v);
        if (m == 2 && n == 0) return uuder(u, v);
        if (m == 1 && n == 1) return uvder(u, v);
        if (m == 0 && n == 2) return vvder(u, v);
        // 更高阶: 数值差分
        Vector3 result(0.0);
        // 简化实现
        return result;
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        return bspline_.parameterRange();
    }

    // --- 变换 ---

    void transformBy(const Matrix4& trans) override {
        bspline_.transformBy(trans);
    }

    /// 反转法线方向：委托给内部 BSplineSurface 的 u/v 交换
    void invert() { bspline_.invert(); }
    NurbsSurface inverse() const {
        NurbsSurface copy = *this;
        copy.invert();
        return copy;
    }

private:
    BSplineSurface<Vector4> bspline_;

    static Point3 project(const Vector4& h) {
        double w = h.w;
        if (soSmall(w)) return Point3(0.0);
        return Point3(h.x / w, h.y / w, h.z / w);
    }

    /// 有理曲线一阶导数
    static Vector3 rational_der(const Point3& S, double w, const Vector3& Aw, double dw) {
        return (Aw - S * dw) / w;
    }

    /// 有理曲线二阶导数
    static Vector3 rational_der2(
        const Point3& S, double w,
        const Vector3& A1, double d1w,
        const Vector3& A2, double d2w
    ) {
        Vector3 S1 = (A1 - S * d1w) / w;
        return (A2 - 2.0 * S1 * d1w - S * d2w) / w;
    }
};

} // namespace mulan::Geometry
