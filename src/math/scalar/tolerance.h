/**
 * @file tolerance.h
 * @brief 容差系统 — CAD 几何判定的统一精度基准
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 设计思路：
 *  - CAD 中两个 double 永远不会精确相等，求交/缝合/分类全靠容差。
 *  - 区分三类容差：
 *      lengthEps —— 长度/距离容差（点重合、距离判定），默认 1e-9
 *      angleEps  —— 角度容差（弧度），默认 1e-9
 *      paramEps  —— 参数域容差（u/v 参数比较），默认 1e-10
 *  - 所有几何比较类查询（contains/isEmpty/点重合）走本系统，不用 ==。
 *  - 提供全局默认 gDefaultTolerance，允许局部构造自定义 Tolerance 覆盖。
 */
#pragma once

#include <cmath>

namespace mulan::math {

/// 容差配置
struct Tolerance {
    double lengthEps = 1e-9;   ///< 长度/距离容差
    double angleEps  = 1e-9;   ///< 角度容差（弧度）
    double paramEps  = 1e-10;  ///< 参数域容差（u/v）

    /// 默认容差（点重合/距离判定的全局基准）
    static const Tolerance& defaultValue() {
        static const Tolerance inst{};
        return inst;
    }

    // ---------- 标量比较 ----------

    /// a == b （长度尺度）
    bool equal(double a, double b) const {
        return abs(a - b) <= lengthEps;
    }
    /// |v| == 0 （长度尺度）
    bool isZero(double v) const {
        return abs(v) <= lengthEps;
    }
    bool lessEqual(double a, double b) const { return a <= b + lengthEps; }
    bool greaterEqual(double a, double b) const { return a >= b - lengthEps; }
    bool less(double a, double b) const { return a < b - lengthEps; }
    bool greater(double a, double b) const { return a > b + lengthEps; }

    // ---------- 参数比较 ----------

    bool paramEqual(double a, double b) const {
        return std::abs(a - b) <= paramEps;
    }
    bool paramIsZero(double v) const {
        return std::abs(v) <= paramEps;
    }

private:
    static double abs(double v) { return v < 0.0 ? -v : v; }
};

/// 全局默认容差引用（便捷访问）
inline const Tolerance& defaultTolerance() {
    return Tolerance::defaultValue();
}

} // namespace mulan::math
