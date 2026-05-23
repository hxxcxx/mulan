/**
 * @file presearch.h
 * @brief 曲线/曲面参数粗搜索（暴力网格搜索）
 *
 * 基于 truck-geotrait::algo::curve::presearch 和 surface::presearch。
 * 提供参数空间的均匀网格搜索，为 Newton 精确求解提供初始值。
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricCurve.h"
#include "../traits/ParametricSurface.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace MulanGeo::Geometry::Algo::Curve {

/// 在参数范围 [t0, t1] 内均匀搜索，返回 subs(t) 最接近 point 的参数 t。
/// division 控制网格密度。
template<typename C>
double presearch(const C& curve, const Point3& point,
                 std::pair<double, double> range, size_t division = 50)
{
    double t0 = range.first, t1 = range.second;
    double res = t0;
    double minDist2 = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i <= division; ++i) {
        double p = static_cast<double>(i) / static_cast<double>(division);
        double t = t0 * (1.0 - p) + t1 * p;
        double dist2 = glm::length2(curve.subs(t) - point);
        if (dist2 < minDist2) {
            minDist2 = dist2;
            res = t;
        }
    }
    return res;
}

/// Newton 法搜索最近参数：最小化 |curve(t) - point|^2。
/// 函数: f(t) = der(t) · (subs(t) - point), 导数: f'(t) = der2(t)·(subs(t)-point) + |der(t)|^2
template<typename C>
std::optional<double> searchNearestParameter(const C& curve, const Point3& point,
                                               double hint, size_t trials = 100)
{
    for (size_t i = 0; i <= trials; ++i) {
        Point3 diff = curve.subs(hint) - point;
        Vector3 der = curve.der(hint);
        Vector3 der2 = curve.der2(hint);
        double value = glm::dot(der, diff);
        double derivation = glm::dot(der2, diff) + glm::length2(der);
        if (soSmall(derivation)) return std::nullopt;
        double next = hint - value / derivation;
        if (near2(hint, next)) return hint;
        hint = next;
    }
    return std::nullopt;
}

/// Newton 法搜索参数：找到 t 使得 curve(t) = point。
/// 函数: f(t) = der(t) · (subs(t) - point), 导数: f'(t) = |der(t)|^2
/// 验证：subs(t) ≈ point
template<typename C>
std::optional<double> searchParameter(const C& curve, const Point3& point,
                                       double hint, size_t trials = 100)
{
    for (size_t i = 0; i <= trials; ++i) {
        Point3 diff = curve.subs(hint) - point;
        Vector3 der = curve.der(hint);
        double value = glm::dot(der, diff);
        double derivation = glm::length2(der);
        if (soSmall(derivation)) return std::nullopt;
        double next = hint - value / derivation;
        if (near2(hint, next)) {
            if (near(curve.subs(hint), point)) return hint;
            return std::nullopt;
        }
        hint = next;
    }
    return std::nullopt;
}

/// 结合 presearch + Newton 的搜索最近参数。
/// SPHint1D 提供 Parameter 或 Range 提示，否则用 range 做暴力搜索。
template<typename C>
std::optional<double> searchNearestParameterWithHint(
    const C& curve, const Point3& point,
    const SPHint1D& hint, size_t trials = 100)
{
    double t0;
    if (hint.kind == SPHint1DKind::Parameter) {
        t0 = hint.parameter;
    } else if (hint.kind == SPHint1DKind::Range) {
        t0 = presearch(curve, point, hint.range, PRESEARCH_DIVISION);
    } else {
        auto range = curve.rangeTuple();
        t0 = presearch(curve, point, range, PRESEARCH_DIVISION);
    }
    return searchNearestParameter(curve, point, t0, trials);
}

/// 结合 presearch + Newton 的搜索参数。
template<typename C>
std::optional<double> searchParameterWithHint(
    const C& curve, const Point3& point,
    const SPHint1D& hint, size_t trials = 100)
{
    double t0;
    if (hint.kind == SPHint1DKind::Parameter) {
        t0 = hint.parameter;
    } else if (hint.kind == SPHint1DKind::Range) {
        t0 = presearch(curve, point, hint.range, PRESEARCH_DIVISION);
    } else {
        auto range = curve.rangeTuple();
        t0 = presearch(curve, point, range, PRESEARCH_DIVISION);
    }
    return searchParameter(curve, point, t0, trials);
}

/// 参数分割：曲线自适应细分（用于渲染/网格化）
/// 返回 (参数节点, 对应点) 对
template<typename C>
std::pair<std::vector<double>, std::vector<Point3>> parameterDivision(
    const C& curve, std::pair<double, double> range, double tol)
{
    double t0 = range.first, t1 = range.second;
    Point3 p0 = curve.subs(t0);
    Point3 p1 = curve.subs(t1);
    return subParameterDivision(curve, range, p0, p1, tol, 100);
}

template<typename C>
std::pair<std::vector<double>, std::vector<Point3>> subParameterDivision(
    const C& curve, std::pair<double, double> range,
    const Point3& p0, const Point3& p1, double tol, size_t trials)
{
    double t0 = range.first, t1 = range.second;
    double tm = (t0 + t1) / 2.0;
    Point3 pm = curve.subs(tm);
    Point3 mid = p0 + (p1 - p0) * 0.5;
    double dist2 = glm::length2(pm - mid);

    if (dist2 < tol * tol || trials == 0) {
        return {{t0, t1}, {p0, p1}};
    }

    auto [params0, pts0] = subParameterDivision(curve, {t0, tm}, p0, pm, tol, trials - 1);
    auto [params1, pts1] = subParameterDivision(curve, {tm, t1}, pm, p1, tol, trials - 1);

    params0.pop_back();
    pts0.pop_back();
    params0.insert(params0.end(), params1.begin(), params1.end());
    pts0.insert(pts0.end(), pts1.begin(), pts1.end());
    return {params0, pts0};
}

} // namespace MulanGeo::Geometry::Algo::Curve