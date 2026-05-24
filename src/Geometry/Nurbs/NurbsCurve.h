/**
 * @file NurbsCurve.h
 * @brief NURBS 曲线 (非均匀有理 B样条)
 *
 * 基于 truck-geometry::nurbs::NurbsCurve。
 * NURBS = 齐次坐标 B-spline + 权重除法。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricCurve.h"
#include "BSplineCurve.h"
#include "../Export.h"

namespace MulanGeo::geometry {

/// NURBS 曲线 — 内部持有齐次坐标 B-spline 曲线
class GEOMETRY_API NurbsCurve
    : public BoundedCurve<Point3, Vector3> {
public:
    NurbsCurve() = default;

    /// 从齐次坐标 B-spline 曲线构造
    explicit NurbsCurve(BSplineCurve<Vector4> homogeneous_curve)
        : bspline_(std::move(homogeneous_curve)) {}

    /// 从节点向量和 3D 控制点 + 权重构造
    NurbsCurve(KnotVec knotVec, std::vector<Point3> points, std::vector<double> weights)
        : bspline_(make_homogeneous(std::move(knotVec), points, weights)) {}

    const BSplineCurve<Vector4>& nonRationalized() const { return bspline_; }
    const KnotVec& knotVec() const { return bspline_.knotVec(); }
    size_t degree() const { return bspline_.degree(); }

    // --- ParametricCurve 接口 ---

    Point3 subs(double t) const override {
        Vector4 h = bspline_.subs(t);
        return project(h);
    }

    Vector3 der(double t) const override {
        auto ders0 = bspline_.ders(1, t);
        // 有理曲线导数: C'(t) = (P'(t) - w'(t)*C(t)) / w(t)
        Vector4 h0 = ders0[0];
        Vector4 h1 = ders0[1];
        Point3 c = project(h0);
        double w = h0.w;
        double dw = h1.w;
        Vector3 dp(h1.x, h1.y, h1.z);
        return (dp - c * dw) / w;
    }

    Vector3 der2(double t) const override {
        auto d = bspline_.ders(2, t);
        double w = d[0].w;
        Vector3 A(d[0].x, d[0].y, d[0].z);
        Vector3 A1(d[1].x, d[1].y, d[1].z);
        Vector3 A2(d[2].x, d[2].y, d[2].z);
        double w1 = d[1].w;
        double w2 = d[2].w;

        Vector3 C = A / w;
        Vector3 C1 = (A1 - C * w1) / w;
        return (A2 - 2.0 * C1 * w1 - C * w2) / w;
    }

    Vector3 derN(size_t n, double t) const override {
        auto d = bspline_.ders(n, t);
        auto rat = d.ratDers();
        return rat[n];
    }

    CurveDers<Vector3> ders(size_t n, double t) const override {
        auto d = bspline_.ders(n, t);
        return d.ratDers();
    }

    ParameterRange parameterRange() const override {
        return bspline_.parameterRange();
    }

    std::optional<double> period() const override { return std::nullopt; }

    // --- BoundedCurve ---

    std::pair<double, double> rangeTuple() const override {
        return bspline_.rangeTuple();
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<Point3>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        // 使用底层的齐次 B-spline 参数分割
        auto [params, hpoints] = bspline_.parameterDivision(range, tol);
        std::vector<Point3> points;
        points.reserve(hpoints.size());
        for (const auto& hp : hpoints) {
            points.push_back(project(hp));
        }
        return {params, points};
    }

    // --- 变换 ---

    void transformBy(const Matrix4& trans) override {
        bspline_.transformBy(trans);
    }

    // --- 方向反转 (Invertible) ---

    void invert() {
        bspline_.invert();
    }

    NurbsCurve inverse() const {
        NurbsCurve c(*this);
        c.invert();
        return c;
    }

    // --- 切割 ---

    /// 在参数 t 处切割 NURBS 曲线，返回 (左半段, 右半段)
    std::pair<NurbsCurve, NurbsCurve> cut(double t) const {
        auto [left, right] = bspline_.cut(t);
        return {NurbsCurve(std::move(left)), NurbsCurve(std::move(right))};
    }

private:
    BSplineCurve<Vector4> bspline_;

    /// 齐次坐标投影到 3D
    static Point3 project(const Vector4& h) {
        double w = h.w;
        if (soSmall(w)) return Point3(0.0);
        return Point3(h.x / w, h.y / w, h.z / w);
    }

    /// 从 3D 点 + 权重构造齐次坐标
    static BSplineCurve<Vector4> make_homogeneous(
        KnotVec kv, const std::vector<Point3>& pts, const std::vector<double>& weights
    ) {
        std::vector<Vector4> hpts;
        hpts.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            double w = (i < weights.size()) ? weights[i] : 1.0;
            hpts.push_back(Vector4(pts[i] * w, w));
        }
        return BSplineCurve<Vector4>(std::move(kv), std::move(hpts));
    }
};

} // namespace MulanGeo::Geometry
