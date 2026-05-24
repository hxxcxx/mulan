/**
 * @file CurveOps.h
 * @brief Curve / Surface 的 visitor 分发函数
 *
 * 所有求值操作通过 std::visit 分发到底层几何类型。
 * IntersectionCurve 的求值委托给 leader 曲线。
 *
 * 基于 truck-modeling 的各项操作。
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "CurveSurface.h"
#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Algo/curve/presearch.h>
#include <MulanGeo/Geometry/Algo/surface/search.h>
#include <utility>

namespace mulan::brep {

// ============================================================
// 自由函数 (free function) 分发
// ============================================================

// --- Curve 求值 ---

inline geometry::Point3 curve_subs(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> geometry::Point3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            // 交线求值委托给 leader
            return g.leader->subs(t);
        } else {
            return g.subs(t);
        }
    }, c.variant());
}

inline geometry::Vector3 curve_der(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->der(t);
        } else {
            return g.der(t);
        }
    }, c.variant());
}

inline geometry::Vector3 curve_der2(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->der2(t);
        } else {
            return g.der2(t);
        }
    }, c.variant());
}

inline geometry::Vector3 curve_derN(const Curve& c, size_t n, double t) {
    return std::visit([n, t](const auto& g) -> geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->derN(n, t);
        } else {
            return g.derN(n, t);
        }
    }, c.variant());
}

inline geometry::CurveDers<geometry::Vector3> curve_ders(const Curve& c, size_t n, double t) {
    return std::visit([n, t](const auto& g) -> geometry::CurveDers<geometry::Vector3> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->ders(n, t);
        } else {
            return g.ders(n, t);
        }
    }, c.variant());
}

inline geometry::ParameterRange curve_parameterRange(const Curve& c) {
    return std::visit([](const auto& g) -> geometry::ParameterRange {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->parameterRange();
        } else {
            return g.parameterRange();
        }
    }, c.variant());
}

inline std::optional<double> curve_period(const Curve& c) {
    return std::visit([](const auto& g) -> std::optional<double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->period();
        } else {
            return g.period();
        }
    }, c.variant());
}

inline std::pair<double, double> curve_rangeTuple(const Curve& c) {
    return std::visit([](const auto& g) -> std::pair<double, double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->rangeTuple();
        } else {
            return g.rangeTuple();
        }
    }, c.variant());
}

inline geometry::Point3 curve_front(const Curve& c) {
    auto [t0, t1] = curve_rangeTuple(c);
    (void)t1;
    return curve_subs(c, t0);
}

inline geometry::Point3 curve_back(const Curve& c) {
    auto [t0, t1] = curve_rangeTuple(c);
    (void)t0;
    return curve_subs(c, t1);
}

inline std::pair<std::vector<double>, std::vector<geometry::Point3>>
curve_parameterDivision(const Curve& c, std::pair<double, double> range, double tol) {
    return std::visit([&](const auto& g)
        -> std::pair<std::vector<double>, std::vector<geometry::Point3>> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->parameterDivision(range, tol);
        } else {
            return g.parameterDivision(range, tol);
        }
    }, c.variant());
}

inline void curve_transformBy(Curve& c, const geometry::Matrix4& mat) {
    std::visit([&mat](auto& g) {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            g.leader->transformBy(mat);
            if (g.surface0) g.surface0->transformBy(mat);
            if (g.surface1) g.surface1->transformBy(mat);
        } else {
            g.transformBy(mat);
        }
    }, c.variant());
}

// --- 曲线参数反求 (search parameter) ---

inline std::optional<double> curve_searchNearestParameter(
    const Curve& c, const geometry::Point3& point, double hint, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return geometry::Algo::Curve::searchNearestParameter(*g.leader, point, hint, trials);
        } else {
            return geometry::Algo::Curve::searchNearestParameter(g, point, hint, trials);
        }
    }, c.variant());
}

inline std::optional<double> curve_searchParameter(
    const Curve& c, const geometry::Point3& point, double hint, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return geometry::Algo::Curve::searchParameter(*g.leader, point, hint, trials);
        } else {
            return geometry::Algo::Curve::searchParameter(g, point, hint, trials);
        }
    }, c.variant());
}

inline std::optional<double> curve_searchNearestParameterWithHint(
    const Curve& c, const geometry::Point3& point,
    const geometry::SPHint1D& hint = {}, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return geometry::Algo::Curve::searchNearestParameterWithHint(*g.leader, point, hint, trials);
        } else {
            return geometry::Algo::Curve::searchNearestParameterWithHint(g, point, hint, trials);
        }
    }, c.variant());
}

inline std::optional<double> curve_searchParameterWithHint(
    const Curve& c, const geometry::Point3& point,
    const geometry::SPHint1D& hint = {}, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<double> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return geometry::Algo::Curve::searchParameterWithHint(*g.leader, point, hint, trials);
        } else {
            return geometry::Algo::Curve::searchParameterWithHint(g, point, hint, trials);
        }
    }, c.variant());
}

// --- 曲面参数反求 (search parameter) ---

inline std::optional<std::pair<double, double>> surface_searchNearestParameter(
    const Surface& s, const geometry::Point3& point,
    std::pair<double, double> hint, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<std::pair<double, double>> {
        return geometry::Algo::Surface::searchNearestParameter(g, point, hint, trials);
    }, s.variant());
}

inline std::optional<std::pair<double, double>> surface_searchParameter(
    const Surface& s, const geometry::Point3& point,
    std::pair<double, double> hint, size_t trials = 100)
{
    return std::visit([&](const auto& g) -> std::optional<std::pair<double, double>> {
        return geometry::Algo::Surface::searchParameter(g, point, hint, trials);
    }, s.variant());
}

inline std::optional<std::pair<double, double>> surface_searchNearestParameterWithHint(
    const Surface& s, const geometry::Point3& point,
    const geometry::SPHint2D& hint = {}, size_t trials = 100, size_t division = 50)
{
    return std::visit([&](const auto& g) -> std::optional<std::pair<double, double>> {
        return geometry::Algo::Surface::searchNearestParameterWithHint(g, point, hint, trials, division);
    }, s.variant());
}

inline std::optional<std::pair<double, double>> surface_searchParameterWithHint(
    const Surface& s, const geometry::Point3& point,
    const geometry::SPHint2D& hint = {}, size_t trials = 100, size_t division = 50)
{
    return std::visit([&](const auto& g) -> std::optional<std::pair<double, double>> {
        return geometry::Algo::Surface::searchParameterWithHint(g, point, hint, trials, division);
    }, s.variant());
}

// --- Curve invert ---

inline void curve_invert(Curve& c) {
    std::visit([](auto& g) {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            g.leader->invert();
        } else {
            g.invert();
        }
    }, c.variant());
}

inline Curve curve_inverse(const Curve& c) {
    Curve copy = c;
    copy.invert();
    return copy;
}

// --- Surface 求值 ---

inline geometry::Point3 surface_subs(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Point3 {
        return g.subs(u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_uder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Vector3 {
        return g.uder(u, v);
    }, s.variant());
}

// --- IncludeCurve: 检查曲线是否在曲面上 ---

inline bool surface_includeCurve(const Surface& s, const Curve& c, double tol = -1.0) {
    if (tol < 0.0) tol = geometry::TOLERANCE;
    auto range = curve_rangeTuple(c);
    const size_t n = 32;
    for (size_t i = 0; i <= n; ++i) {
        double t = range.first + (range.second - range.first) * i / n;
        geometry::Point3 pt_on_curve = curve_subs(c, t);
        auto uv = surface_searchNearestParameter(s, pt_on_curve, {0.0, 0.0}, 50);
        if (!uv) return false;
        geometry::Point3 pt_on_surface = surface_subs(s, uv->first, uv->second);
        if (glm::distance(pt_on_curve, pt_on_surface) > tol) return false;
    }
    return true;
}

inline geometry::Vector3 surface_vder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Vector3 {
        return g.vder(u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_uuder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Vector3 {
        return g.uuder(u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_uvder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Vector3 {
        return g.uvder(u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_vvder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> geometry::Vector3 {
        return g.vvder(u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_derMN(const Surface& s, size_t m, size_t n, double u, double v) {
    return std::visit([m, n, u, v](const auto& g) -> geometry::Vector3 {
        return g.derMN(m, n, u, v);
    }, s.variant());
}

inline geometry::Vector3 surface_normal(const Surface& s, double u, double v) {
    auto du = surface_uder(s, u, v);
    auto dv = surface_vder(s, u, v);
    return glm::normalize(glm::cross(du, dv));
}

inline std::pair<geometry::ParameterRange, geometry::ParameterRange>
surface_parameterRange(const Surface& s) {
    return std::visit([](const auto& g)
        -> std::pair<geometry::ParameterRange, geometry::ParameterRange> {
        return g.parameterRange();
    }, s.variant());
}

inline void surface_transformBy(Surface& s, const geometry::Matrix4& mat) {
    std::visit([&mat](auto& g) {
        g.transformBy(mat);
    }, s.variant());
}

// --- 曲线拼接 (concat) ---

/// 将两条曲线的几何拼接成一条连续曲线
/// 假定 c0 的终点 == c1 的起点
/// 策略: 提升为 NURBS (4D B-spline), 合并 knot 向量和控制点
inline Curve curve_concat(const Curve& c0, const Curve& c1) {
    using namespace Geometry;

    // 简单情况：两条 Line 直接连接
    if (c0.holds<Line<Point3>>() && c1.holds<Line<Point3>>()) {
        const auto& l0 = c0.get<Line<Point3>>();
        const auto& l1 = c1.get<Line<Point3>>();
        return Curve(Line<Point3>(l0.front(), l1.back()));
    }

    // 提升为 4D 齐次 B-spline
    auto bsp0 = c0.liftUp();
    auto bsp1 = c1.liftUp();

    // 拷贝并归一化 knot 向量到 [0,1]
    auto knots0_vec = bsp0.knotVec().as_vec();
    auto knots1_vec = bsp1.knotVec().as_vec();

    double r0_len = knots0_vec.back() - knots0_vec.front();
    double r1_len = knots1_vec.back() - knots1_vec.front();
    if (r0_len < TOLERANCE || r1_len < TOLERANCE) return c0;

    double off0 = knots0_vec.front();
    for (auto& k : knots0_vec) k = (k - off0) / r0_len;

    double off1 = knots1_vec.front();
    for (auto& k : knots1_vec) k = (k - off1) / r1_len + 1.0;

    // 合并 knot 向量 (bsp0 的 + bsp1 去掉第一个)
    std::vector<double> merged_knots = knots0_vec;
    for (size_t i = 1; i < knots1_vec.size(); ++i) {
        merged_knots.push_back(knots1_vec[i]);
    }

    // 合并控制点（去掉 bsp1 的第一个，它和 bsp0 的最后一个重合）
    auto cps0 = bsp0.controlPoints();
    auto cps1 = bsp1.controlPoints();
    std::vector<Vector4> merged_cps = cps0;
    for (size_t i = 1; i < cps1.size(); ++i) {
        merged_cps.push_back(cps1[i]);
    }

    // 归一化到 [0, 1]
    double total_len = knots1_vec.back();
    for (auto& k : merged_knots) k /= total_len;

    KnotVec knot_vec(std::move(merged_knots));
    return Curve(NurbsCurve(BSplineCurve<Vector4>(std::move(knot_vec), std::move(merged_cps))));
}

// ============================================================
// Curve 类方法实现 (内联，委托给自由函数)
// ============================================================

inline geometry::Point3 Curve::subs(double t) const { return curve_subs(*this, t); }
inline geometry::Vector3 Curve::der(double t) const { return curve_der(*this, t); }
inline geometry::Vector3 Curve::der2(double t) const { return curve_der2(*this, t); }
inline geometry::Vector3 Curve::derN(size_t n, double t) const { return curve_derN(*this, n, t); }
inline geometry::CurveDers<geometry::Vector3> Curve::ders(size_t n, double t) const { return curve_ders(*this, n, t); }
inline geometry::ParameterRange Curve::parameterRange() const { return curve_parameterRange(*this); }
inline std::optional<double> Curve::period() const { return curve_period(*this); }
inline std::pair<double, double> Curve::rangeTuple() const { return curve_rangeTuple(*this); }
inline geometry::Point3 Curve::front() const { return curve_front(*this); }
inline geometry::Point3 Curve::back() const { return curve_back(*this); }
inline std::pair<std::vector<double>, std::vector<geometry::Point3>>
    Curve::parameterDivision(std::pair<double, double> range, double tol) const {
    return curve_parameterDivision(*this, range, tol);
}
inline void Curve::transformBy(const geometry::Matrix4& mat) { curve_transformBy(*this, mat); }
inline void Curve::invert() { curve_invert(*this); }
inline Curve Curve::inverse() const { return curve_inverse(*this); }
inline std::optional<double> Curve::searchNearestParameter(const geometry::Point3& point, double hint, size_t trials) const { return curve_searchNearestParameter(*this, point, hint, trials); }
inline std::optional<double> Curve::searchParameter(const geometry::Point3& point, double hint, size_t trials) const { return curve_searchParameter(*this, point, hint, trials); }
inline Curve Curve::concat(const Curve& other) const { return curve_concat(*this, other); }

// ============================================================
// Curve::liftUp — 提升为 4D 齐次 B样条
// ============================================================

inline geometry::BSplineCurve<geometry::Vector4> Curve::liftUp() const {
    using geometry::Vector4;
    using geometry::BSplineCurve;
    using geometry::KnotVec;
    using geometry::NurbsCurve;
    using geometry::Point3;

    if (holds<geometry::Line<Point3>>()) {
        auto& line = get<geometry::Line<Point3>>();
        Point3 p0 = line.frontPoint();
        Point3 p1 = line.backPoint();
        std::vector<Vector4> cps = {
            Vector4(p0, 1.0),
            Vector4(p1, 1.0)
        };
        KnotVec knots = KnotVec::bezier_knot(1);
        return BSplineCurve<Vector4>(std::move(knots), std::move(cps));
    }

    if (holds<geometry::BSplineCurve<Point3>>()) {
        auto& bspline = get<geometry::BSplineCurve<Point3>>();
        std::vector<Vector4> cps;
        cps.reserve(bspline.controlPoints().size());
        for (const auto& p : bspline.controlPoints()) {
            cps.push_back(Vector4(p, 1.0));
        }
        return BSplineCurve<Vector4>(bspline.knotVec(), std::move(cps));
    }

    if (holds<NurbsCurve>()) {
        return get<NurbsCurve>().nonRationalized();
    }

    if (holds<IntersectionCurve>()) {
        return get<IntersectionCurve>().leader->liftUp();
    }

    return BSplineCurve<Vector4>();
}

// ============================================================
// Surface 类方法实现 (内联，委托给自由函数)
// ============================================================

inline geometry::Point3 Surface::subs(double u, double v) const { return surface_subs(*this, u, v); }
inline geometry::Vector3 Surface::uder(double u, double v) const { return surface_uder(*this, u, v); }
inline geometry::Vector3 Surface::vder(double u, double v) const { return surface_vder(*this, u, v); }
inline geometry::Vector3 Surface::uuder(double u, double v) const { return surface_uuder(*this, u, v); }
inline geometry::Vector3 Surface::uvder(double u, double v) const { return surface_uvder(*this, u, v); }
inline geometry::Vector3 Surface::vvder(double u, double v) const { return surface_vvder(*this, u, v); }
inline geometry::Vector3 Surface::derMN(size_t m, size_t n, double u, double v) const { return surface_derMN(*this, m, n, u, v); }
inline geometry::Vector3 Surface::normal(double u, double v) const { return surface_normal(*this, u, v); }
inline std::pair<geometry::ParameterRange, geometry::ParameterRange> Surface::parameterRange() const { return surface_parameterRange(*this); }
inline void Surface::transformBy(const geometry::Matrix4& mat) { surface_transformBy(*this, mat); }

// --- Surface invert ---

inline void surface_invert(Surface& s) {
    std::visit([](auto& g) { g.invert(); }, s.variant());
}

inline Surface surface_inverse(const Surface& s) {
    Surface copy = s;
    copy.invert();
    return copy;
}

inline void Surface::invert() { surface_invert(*this); }
inline Surface Surface::inverse() const { return surface_inverse(*this); }

} // namespace mulan::BRep
