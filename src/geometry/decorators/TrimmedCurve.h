/**
 * @file TrimmedCurve.h
 * @brief 裁剪曲线 (限制参数范围)
 *
 * 基于 truck-geometry::decorators::TrimmedCurve。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"
#include <memory>

namespace mulan::geometry {

/// 裁剪曲线: 将底层曲线限制在参数范围 [t0, t1]
template<typename Curve>
class TrimmedCurve : public BoundedCurve<Point3, Vector3> {
public:
    TrimmedCurve() = default;

    /// 构造裁剪曲线
    /// @param curve 底层曲线
    /// @param t0 参数下界
    /// @param t1 参数上界
    TrimmedCurve(Curve curve, double t0, double t1)
        : curve_(std::move(curve))
        , t0_(t0)
        , t1_(t1) {}

    const Curve& innerCurve() const { return curve_; }
    double frontParam() const { return t0_; }
    double backParam() const { return t1_; }

    // --- ParametricCurve ---

    Point3 subs(double t) const override { return curve_.subs(t); }
    Vector3 der(double t) const override { return curve_.der(t); }
    Vector3 der2(double t) const override { return curve_.der2(t); }
    Vector3 derN(size_t n, double t) const override { return curve_.derN(n, t); }

    ParameterRange parameterRange() const override {
        return {{BoundKind::Included, t0_}, {BoundKind::Included, t1_}};
    }

    // --- BoundedCurve ---

    std::pair<double, double> rangeTuple() const override { return {t0_, t1_}; }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<Point3>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        return curve_.parameterDivision(range, tol);
    }

    void transformBy(const Matrix4& mat) override {
        curve_.transformBy(mat);
    }

    void invert() { std::swap(t0_, t1_); }
    TrimmedCurve inverse() const { return TrimmedCurve(curve_, t1_, t0_); }

private:
    Curve curve_;
    double t0_ = 0.0;
    double t1_ = 1.0;
};

} // namespace mulan::geometry
