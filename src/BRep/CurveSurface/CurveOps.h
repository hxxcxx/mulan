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
#include <utility>

namespace MulanGeo::BRep {

// ============================================================
// 自由函数 (free function) 分发
// ============================================================

// --- Curve 求值 ---

inline Geometry::Point3 curve_subs(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> Geometry::Point3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            // 交线求值委托给 leader
            return g.leader->subs(t);
        } else {
            return g.subs(t);
        }
    }, c.variant());
}

inline Geometry::Vector3 curve_der(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> Geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->der(t);
        } else {
            return g.der(t);
        }
    }, c.variant());
}

inline Geometry::Vector3 curve_der2(const Curve& c, double t) {
    return std::visit([t](const auto& g) -> Geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->der2(t);
        } else {
            return g.der2(t);
        }
    }, c.variant());
}

inline Geometry::Vector3 curve_derN(const Curve& c, size_t n, double t) {
    return std::visit([n, t](const auto& g) -> Geometry::Vector3 {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->derN(n, t);
        } else {
            return g.derN(n, t);
        }
    }, c.variant());
}

inline Geometry::CurveDers<Geometry::Vector3> curve_ders(const Curve& c, size_t n, double t) {
    return std::visit([n, t](const auto& g) -> Geometry::CurveDers<Geometry::Vector3> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->ders(n, t);
        } else {
            return g.ders(n, t);
        }
    }, c.variant());
}

inline Geometry::ParameterRange curve_parameterRange(const Curve& c) {
    return std::visit([](const auto& g) -> Geometry::ParameterRange {
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

inline Geometry::Point3 curve_front(const Curve& c) {
    auto [t0, t1] = curve_rangeTuple(c);
    (void)t1;
    return curve_subs(c, t0);
}

inline Geometry::Point3 curve_back(const Curve& c) {
    auto [t0, t1] = curve_rangeTuple(c);
    (void)t0;
    return curve_subs(c, t1);
}

inline std::pair<std::vector<double>, std::vector<Geometry::Point3>>
curve_parameterDivision(const Curve& c, std::pair<double, double> range, double tol) {
    return std::visit([&](const auto& g)
        -> std::pair<std::vector<double>, std::vector<Geometry::Point3>> {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            return g.leader->parameterDivision(range, tol);
        } else {
            return g.parameterDivision(range, tol);
        }
    }, c.variant());
}

inline void curve_transformBy(Curve& c, const Geometry::Matrix4& mat) {
    std::visit([&mat](auto& g) {
        using T = std::decay_t<decltype(g)>;
        if constexpr (std::is_same_v<T, IntersectionCurve>) {
            g.leader->transformBy(mat);
            // surface0/1 也需要变换
            if (g.surface0) g.surface0->transformBy(mat);
            if (g.surface1) g.surface1->transformBy(mat);
        } else {
            g.transformBy(mat);
        }
    }, c.variant());
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

inline Geometry::Point3 surface_subs(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Point3 {
        return g.subs(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_uder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Vector3 {
        return g.uder(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_vder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Vector3 {
        return g.vder(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_uuder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Vector3 {
        return g.uuder(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_uvder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Vector3 {
        return g.uvder(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_vvder(const Surface& s, double u, double v) {
    return std::visit([u, v](const auto& g) -> Geometry::Vector3 {
        return g.vvder(u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_derMN(const Surface& s, size_t m, size_t n, double u, double v) {
    return std::visit([m, n, u, v](const auto& g) -> Geometry::Vector3 {
        return g.derMN(m, n, u, v);
    }, s.variant());
}

inline Geometry::Vector3 surface_normal(const Surface& s, double u, double v) {
    auto du = surface_uder(s, u, v);
    auto dv = surface_vder(s, u, v);
    return glm::normalize(glm::cross(du, dv));
}

inline std::pair<Geometry::ParameterRange, Geometry::ParameterRange>
surface_parameterRange(const Surface& s) {
    return std::visit([](const auto& g)
        -> std::pair<Geometry::ParameterRange, Geometry::ParameterRange> {
        return g.parameterRange();
    }, s.variant());
}

inline void surface_transformBy(Surface& s, const Geometry::Matrix4& mat) {
    std::visit([&mat](auto& g) {
        g.transformBy(mat);
    }, s.variant());
}

// ============================================================
// Curve 类方法实现 (内联，委托给自由函数)
// ============================================================

inline Geometry::Point3 Curve::subs(double t) const { return curve_subs(*this, t); }
inline Geometry::Vector3 Curve::der(double t) const { return curve_der(*this, t); }
inline Geometry::Vector3 Curve::der2(double t) const { return curve_der2(*this, t); }
inline Geometry::Vector3 Curve::derN(size_t n, double t) const { return curve_derN(*this, n, t); }
inline Geometry::CurveDers<Geometry::Vector3> Curve::ders(size_t n, double t) const { return curve_ders(*this, n, t); }
inline Geometry::ParameterRange Curve::parameterRange() const { return curve_parameterRange(*this); }
inline std::optional<double> Curve::period() const { return curve_period(*this); }
inline std::pair<double, double> Curve::rangeTuple() const { return curve_rangeTuple(*this); }
inline Geometry::Point3 Curve::front() const { return curve_front(*this); }
inline Geometry::Point3 Curve::back() const { return curve_back(*this); }
inline std::pair<std::vector<double>, std::vector<Geometry::Point3>>
    Curve::parameterDivision(std::pair<double, double> range, double tol) const {
    return curve_parameterDivision(*this, range, tol);
}
inline void Curve::transformBy(const Geometry::Matrix4& mat) { curve_transformBy(*this, mat); }
inline void Curve::invert() { curve_invert(*this); }
inline Curve Curve::inverse() const { return curve_inverse(*this); }

// ============================================================
// Surface 类方法实现 (内联，委托给自由函数)
// ============================================================

inline Geometry::Point3 Surface::subs(double u, double v) const { return surface_subs(*this, u, v); }
inline Geometry::Vector3 Surface::uder(double u, double v) const { return surface_uder(*this, u, v); }
inline Geometry::Vector3 Surface::vder(double u, double v) const { return surface_vder(*this, u, v); }
inline Geometry::Vector3 Surface::uuder(double u, double v) const { return surface_uuder(*this, u, v); }
inline Geometry::Vector3 Surface::uvder(double u, double v) const { return surface_uvder(*this, u, v); }
inline Geometry::Vector3 Surface::vvder(double u, double v) const { return surface_vvder(*this, u, v); }
inline Geometry::Vector3 Surface::derMN(size_t m, size_t n, double u, double v) const { return surface_derMN(*this, m, n, u, v); }
inline Geometry::Vector3 Surface::normal(double u, double v) const { return surface_normal(*this, u, v); }
inline std::pair<Geometry::ParameterRange, Geometry::ParameterRange> Surface::parameterRange() const { return surface_parameterRange(*this); }
inline void Surface::transformBy(const Geometry::Matrix4& mat) { surface_transformBy(*this, mat); }

} // namespace MulanGeo::BRep
