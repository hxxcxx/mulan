/**
 * @file Interval.h
 * @brief 区间（1D / 2D 参数域）— 用于曲线曲面参数空间
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 设计理由：
 *  - CAD 曲线/曲面都有参数域（u∈[u0,u1]、(u,v)∈[u0,u1]×[v0,v1]）。
 *  - 区间类型封装 [min,max] 的判包含、钳制、求交、归一化等，
 *    为下期 curve 模块铺路，本模块求交也可复用。
 */
#pragma once

#include "GeoMath.h"
#include "Tolerance.h"

#include <algorithm>
#include <limits>

namespace mulan::geo {

// ============================================================
// Interval — 1D 闭区间 [min, max]
// ============================================================

struct Interval {
    double min{};
    double max{};

    constexpr Interval() = default;
    constexpr Interval(double v) : min(v), max(v) {}                  // 退化区间（单点）
    constexpr Interval(double lo, double hi) : min(lo), max(hi) {}

    static constexpr Interval empty() {
        return Interval(std::numeric_limits<double>::max(),
                       -std::numeric_limits<double>::max());   // min>max → 空
    }
    static constexpr Interval unbounded() {
        return Interval(-std::numeric_limits<double>::max(),
                        std::numeric_limits<double>::max());
    }

    // ---------- 状态 ----------

    bool isEmpty(const Tolerance& tol = defaultTolerance()) const {
        return min > max + tol.paramEps;
    }
    bool isPoint(const Tolerance& tol = defaultTolerance()) const {
        return std::abs(max - min) <= tol.paramEps;
    }
    double length() const { return max - min; }
    double center() const { return (min + max) * 0.5; }

    // ---------- 查询 ----------

    /// v 是否落在区间内（闭区间）
    bool contains(double v, const Tolerance& tol = defaultTolerance()) const {
        return v >= min - tol.paramEps && v <= max + tol.paramEps;
    }

    /// 是否严格在内部（不含端点）
    bool containsInterior(double v, const Tolerance& tol = defaultTolerance()) const {
        return v > min + tol.paramEps && v < max - tol.paramEps;
    }

    /// 将 v 钳制到 [min,max]
    double clamp(double v) const { return geo::clamp(v, min, max); }

    /// 把 v 从本区间线性映射到 [0,1]（归一化参数）
    double toNormalized(double v) const {
        double len = length();
        if (std::abs(len) < 1e-15) return 0.0;
        return (v - min) / len;
    }
    /// 把 [0,1] 的归一化参数映射回本区间
    double fromNormalized(double t) const { return min + (max - min) * t; }

    // ---------- 区间运算 ----------

    /// 与另一区间求交（返回空区间表示不相交）
    Interval intersected(const Interval& o) const {
        double lo = std::max(min, o.min);
        double hi = std::min(max, o.max);
        return Interval(lo, hi);   // 若 lo>hi，isEmpty() 为真
    }

    /// 扩展以包含 v
    void expand(double v) {
        min = std::min(min, v);
        max = std::max(max, v);
    }
    /// 扩展以包含另一区间
    void expand(const Interval& o) {
        if (o.isEmpty()) return;
        min = std::min(min, o.min);
        max = std::max(max, o.max);
    }

    bool operator==(const Interval& o) const { return min == o.min && max == o.max; }
    bool operator!=(const Interval& o) const { return !(*this == o); }
};

// ============================================================
// Interval2 — 2D 参数矩形域 [umin,umax] × [vmin,vmax]
// ============================================================

struct Interval2 {
    Interval u{};
    Interval v{};

    constexpr Interval2() = default;
    constexpr Interval2(const Interval& u_, const Interval& v_) : u(u_), v(v_) {}
    constexpr Interval2(double umin, double umax, double vmin, double vmax)
        : u(umin, umax), v(vmin, vmax) {}

    bool isEmpty(const Tolerance& tol = defaultTolerance()) const {
        return u.isEmpty(tol) || v.isEmpty(tol);
    }

    /// (pu,pv) 是否落在域内
    bool contains(double pu, double pv, const Tolerance& tol = defaultTolerance()) const {
        return u.contains(pu, tol) && v.contains(pv, tol);
    }

    Interval2 intersected(const Interval2& o) const {
        return Interval2(u.intersected(o.u), v.intersected(o.v));
    }
};

} // namespace mulan::geo
