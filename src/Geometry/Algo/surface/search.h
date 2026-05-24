/**
 * @file search.h
 * @brief 曲面参数反求算法（暴力网格搜索 + Newton 法）
 *
 * 基于 truck-geotrait::algo::surface。
 * 提供 2D 参数空间的搜索、最近点搜索和交线搜索。
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include <mulan/Geometry/Types.h>
#include <mulan/Geometry/Tolerance.h>
#include <mulan/Geometry/Newton.h>
#include <mulan/Geometry/traits/ParametricSurface.h>
#include <mulan/Geometry/traits/ParametricCurve.h>
#include <mulan/Geometry/traits/SearchParameter.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace mulan::geometry::Algo::Surface {

// ============================================================
// 辅助函数
// ============================================================

inline double boundsToDouble(const Bound& b) {
    return b.value;
}

inline std::pair<double, double> boundsToDoublePair(const ParameterRange& range) {
    return {boundsToDouble(range.first), boundsToDouble(range.second)};
}

template<typename S>
std::pair<double, double> presearch(const S& surface, const Point3& point,
    std::pair<std::pair<double, double>, std::pair<double, double>> range,
    size_t division = 50)
{
    auto [urange, vrange] = range;
    double best_u = urange.first, best_v = vrange.first;
    double minDist2 = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i <= division; ++i) {
        double pu = static_cast<double>(i) / static_cast<double>(division);
        double u = urange.first * (1.0 - pu) + urange.second * pu;
        for (size_t j = 0; j <= division; ++j) {
            double pv = static_cast<double>(j) / static_cast<double>(division);
            double v = vrange.first * (1.0 - pv) + vrange.second * pv;
            double dist2 = glm::length2(surface.subs(u, v) - point);
            if (dist2 < minDist2) {
                minDist2 = dist2;
                best_u = u;
                best_v = v;
            }
        }
    }
    return {best_u, best_v};
}

/// Newton 法搜索最近点参数：最小化 |surface(u,v) - point|^2。
/// 3D 版本：将问题增广为 3x3 系统
///   f = (surface(u,v) - point) + (uder × vder) * w
///   Jacobian = 3x3 matrix
template<typename S>
std::optional<std::pair<double, double>> searchNearestParameter(
    const S& surface, const Point3& point,
    std::pair<double, double> hint, size_t trials = 100)
{
    // 3D Newton: state = (u, v, w), w is a Lagrange multiplier
    double u = hint.first, v = hint.second, w = 0.0;

    for (size_t iter = 0; iter <= trials; ++iter) {
        Point3 diff = surface.subs(u, v) - point;
        Vector3 du = surface.uder(u, v);
        Vector3 dv = surface.vder(u, v);
        Vector3 duu = surface.uuder(u, v);
        Vector3 duv = surface.uvder(u, v);
        Vector3 dvv = surface.vvder(u, v);
        Vector3 uv_cross = glm::cross(du, dv);

        // F = diff + uv_cross * w
        Vector3 F = diff + uv_cross * w;
        glm::dmat3 J(
            du + (glm::cross(duu, dv) + glm::cross(du, duv)) * w,
            dv + (glm::cross(duv, dv) + glm::cross(du, dvv)) * w,
            uv_cross
        );

        auto inv = glm::inverse(J);
        if (glm::isnan(inv[0][0])) return std::nullopt;

        Vector3 delta = inv * F;
        double nu = u - delta.x;
        double nv = v - delta.y;
        double nw = w - delta.z;

        if (near2(u, nu) && near2(v, nv)) {
            return std::make_pair(u, v);
        }
        u = nu; v = nv; w = nw;
    }
    return std::nullopt;
}

/// Newton 法搜索精确参数：找到 (u,v) 使得 surface(u,v) = point。
/// 将 3D 问题投影到切平面，形成 2x2 系统：
///   f = (uder · diff, vder · diff)
///   Jacobian = 2x2 (切向投影)
template<typename S>
std::optional<std::pair<double, double>> searchParameter(
    const S& surface, const Point3& point,
    std::pair<double, double> hint, size_t trials = 100)
{
    double u = hint.first, v = hint.second;

    for (size_t iter = 0; iter <= trials; ++iter) {
        Vector3 diff = surface.subs(u, v) - point;
        Vector3 du = surface.uder(u, v);
        Vector3 dv = surface.vder(u, v);

        Vector2 F(glm::dot(du, diff), glm::dot(dv, diff));
        double uu = glm::dot(du, du);
        double uv = glm::dot(du, dv);
        double vv = glm::dot(dv, dv);
        glm::dmat2 J(uu, uv, uv, vv);

        auto inv = glm::inverse(J);
        if (glm::isnan(inv[0][0])) return std::nullopt;

        Vector2 delta = inv * F;
        double nu = u - delta.x;
        double nv = v - delta.y;

        if (near2(u, nu) && near2(v, nv)) {
            if (near(surface.subs(u, v), point)) {
                return std::make_pair(u, v);
            }
            return std::nullopt;
        }
        u = nu; v = nv;
    }
    return std::nullopt;
}

/// 搜索曲面与曲线的交点参数。
/// 3D Newton: surface.subs(u,v) = curve.subs(t)
/// 状态向量: (u, v, t)
template<typename S, typename C>
std::optional<std::pair<std::pair<double, double>, double>> searchIntersectionParameter(
    const S& surface, std::pair<double, double> hint0,
    const C& curve, double hint1, size_t trials = 100)
{
    double u = hint0.first, v = hint0.second, t = hint1;

    for (size_t iter = 0; iter <= trials; ++iter) {
        Vector3 F = surface.subs(u, v) - curve.subs(t);
        Vector3 du = surface.uder(u, v);
        Vector3 dv = surface.vder(u, v);
        Vector3 ct = curve.der(t);

        glm::dmat3 J(du, dv, -ct);
        auto inv = glm::inverse(J);
        if (glm::isnan(inv[0][0])) return std::nullopt;

        Vector3 delta = inv * F;
        double nu = u - delta.x;
        double nv = v - delta.y;
        double nt = t - delta.z;

        if (near2(u, nu) && near2(v, nv) && near2(t, nt)) {
            if (near(surface.subs(u, v), curve.subs(t))) {
                return std::make_pair(std::make_pair(u, v), t);
            }
            return std::nullopt;
        }
        u = nu; v = nv; t = nt;
    }
    return std::nullopt;
}

/// 结合 presearch + Newton 的搜索最近参数（2D）
template<typename S>
std::optional<std::pair<double, double>> searchNearestParameterWithHint(
    const S& surface, const Point3& point,
    const SPHint2D& hint, size_t trials = 100,
    size_t division = 50)
{
    std::pair<double, double> t0;
    if (hint.kind == SPHint2DKind::Parameter) {
        t0 = hint.parameter;
    } else if (hint.kind == SPHint2DKind::Range) {
        t0 = presearch(surface, point, hint.range, division);
    } else {
        auto ranges = surface.parameterRange();
        auto r0 = std::make_pair(boundsToDouble(ranges.first.first), boundsToDouble(ranges.first.second));
        auto r1 = std::make_pair(boundsToDouble(ranges.second.first), boundsToDouble(ranges.second.second));
        t0 = presearch(surface, point, {r0, r1}, division);
    }
    return searchNearestParameter(surface, point, t0, trials);
}

/// 结合 presearch + Newton 的搜索参数（2D）
template<typename S>
std::optional<std::pair<double, double>> searchParameterWithHint(
    const S& surface, const Point3& point,
    const SPHint2D& hint, size_t trials = 100,
    size_t division = 50)
{
    std::pair<double, double> t0;
    if (hint.kind == SPHint2DKind::Parameter) {
        t0 = hint.parameter;
    } else if (hint.kind == SPHint2DKind::Range) {
        t0 = presearch(surface, point, hint.range, division);
    } else {
        auto ranges = surface.parameterRange();
        auto r0 = boundsToDoublePair(ranges.first);
        auto r1 = boundsToDoublePair(ranges.second);
        t0 = presearch(surface, point, {r0, r1}, division);
    }
    return searchParameter(surface, point, t0, trials);
}

template<typename S>
void subParameterDivision(const S& surface,
    std::vector<double>& udiv, std::vector<double>& vdiv,
    double tol, size_t trials);

/// 曲面参数分割（自适应细分）
template<typename S>
std::pair<std::vector<double>, std::vector<double>> parameterDivision(
    const S& surface,
    std::pair<std::pair<double, double>, std::pair<double, double>> ranges,
    double tol)
{
    auto [urange, vrange] = ranges;
    std::vector<double> udiv = {urange.first, urange.second};
    std::vector<double> vdiv = {vrange.first, vrange.second};
    subParameterDivision(surface, udiv, vdiv, tol, 100);
    return {udiv, vdiv};
}

template<typename S>
void subParameterDivision(const S& surface,
    std::vector<double>& udiv, std::vector<double>& vdiv,
    double tol, size_t trials)
{
    if (trials == 0) return;

    size_t nu = udiv.size() - 1;
    size_t nv = vdiv.size() - 1;
    std::vector<bool> divide_u(nu, false);
    std::vector<bool> divide_v(nv, false);

    for (size_t i = 0; i < nu; ++i) {
        for (size_t j = 0; j < nv; ++j) {
            if (divide_u[i] && divide_v[j]) continue;
            double u0 = udiv[i], u1 = udiv[i + 1];
            double v0 = vdiv[j], v1 = vdiv[j + 1];
            double um = (u0 + u1) / 2.0;
            double vm = (v0 + v1) / 2.0;

            double p = 0.5; // hash-based jitter could be added
            double q = 0.5;
            double ut = u0 * (1.0 - p) + u1 * p;
            double vt = v0 * (1.0 - q) + v1 * q;

            Point3 p0 = surface.subs(ut, vt);
            Point3 p00 = surface.subs(u0, v0);
            Point3 p01 = surface.subs(u0, v1);
            Point3 p10 = surface.subs(u1, v0);
            Point3 p11 = surface.subs(u1, v1);
            Point3 interp = p00 * (1.0 - p) * (1.0 - q)
                           + p01 * (1.0 - p) * q
                           + p10 * p * (1.0 - q)
                           + p11 * p * q;

            if (glm::length2(p0 - interp) > tol * tol) {
                double delu = glm::length((p00 + p01) * 0.5 - p0) + glm::length((p10 + p11) * 0.5 - p0);
                double delv = glm::length((p00 + p10) * 0.5 - p0) + glm::length((p01 + p11) * 0.5 - p0);
                if (delu > delv * 2.0) {
                    divide_u[i] = true;
                } else if (delv > delu * 2.0) {
                    divide_v[j] = true;
                } else {
                    divide_u[i] = true;
                    divide_v[j] = true;
                }
            }
        }
    }

    std::vector<double> new_udiv = {udiv[0]};
    for (size_t i = 0; i < nu; ++i) {
        if (divide_u[i]) new_udiv.push_back((udiv[i] + udiv[i + 1]) / 2.0);
        new_udiv.push_back(udiv[i + 1]);
    }

    std::vector<double> new_vdiv = {vdiv[0]};
    for (size_t j = 0; j < nv; ++j) {
        if (divide_v[j]) new_vdiv.push_back((vdiv[j] + vdiv[j + 1]) / 2.0);
        new_vdiv.push_back(vdiv[j + 1]);
    }

    if (udiv.size() != new_udiv.size() || vdiv.size() != new_vdiv.size()) {
        udiv = std::move(new_udiv);
        vdiv = std::move(new_vdiv);
        subParameterDivision(surface, udiv, vdiv, tol, trials - 1);
    }
}

} // namespace mulan::geometry::Algo::Surface