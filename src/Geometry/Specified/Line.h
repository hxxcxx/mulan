/**
 * @file Line.h
 * @brief 直线 (过两点)
 *
 * 基于 truck-geometry::specifieds::Line。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"

namespace MulanGeo::geometry {

/// 直线: subs(t) = p0 + t * (p1 - p0)
template<typename P>
class Line : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    Line() : p0_(0.0), p1_(1.0) {}
    Line(P p0, P p1) : p0_(std::move(p0)), p1_(std::move(p1)) {}

    const P& frontPoint() const { return p0_; }
    const P& backPoint() const { return p1_; }

    // --- ParametricCurve ---

    P subs(double t) const override { return p0_ + (p1_ - p0_) * t; }

    Diff der(double t) const override { return p1_ - p0_; }

    Diff der2(double t) const override { return Diff(0.0); }

    Diff derN(size_t n, double t) const override {
        return n == 0 ? subs(t) - P(0.0) : (n == 1 ? der(t) : Diff(0.0));
    }

    ParameterRange parameterRange() const override {
        return {{BoundKind::Unbounded, 0.0}, {BoundKind::Unbounded, 0.0}};
    }

    std::optional<double> period() const override { return std::nullopt; }

    // --- BoundedCurve ---

    std::pair<double, double> rangeTuple() const override { return {0.0, 1.0}; }

    void invert() { std::swap(p0_, p1_); }
    Line inverse() const { return Line(p1_, p0_); }

    std::pair<Line, Line> cut(double t) const {
        P mid = subs(t);
        return {Line(p0_, mid), Line(mid, p1_)};
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<P>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        // 直线只需两端点
        (void)tol;
        double t0 = range.first, t1 = range.second;
        return {{t0, t1}, {subs(t0), subs(t1)}};
    }

    void transformBy(const Matrix4& mat) override {
        auto t = [&](const P& p) -> P {
            if constexpr (std::same_as<P, Point2>) {
                auto v = mat * glm::dvec4(p, 0.0, 1.0);
                return Point2(v);
            } else {
                auto v = mat * glm::dvec4(p, 1.0);
                return Point3(v);
            }
        };
        p0_ = t(p0_);
        p1_ = t(p1_);
    }

private:
    P p0_, p1_;
};

} // namespace MulanGeo::Geometry
