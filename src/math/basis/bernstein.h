/**
 * @file bernstein.h
 * @brief Bernstein 基函数 + 二项式系数 — Bezier 曲线/曲面共用
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学背景：
 *   n 次 Bernstein 多项式：B_{i,n}(t) = C(n,i) · t^i · (1-t)^(n-i)，i = 0..n
 *   性质：
 *     - 非负性：t∈[0,1] 时 B_{i,n}(t) ≥ 0
 *     - 单位分解：Σ B_{i,n}(t) = 1（故 Bezier 求值是点的仿射组合）
 *     - 对称性：B_{i,n}(t) = B_{n-i,n}(1-t)
 *
 *   本实现采用直接公式（C(n,i) · t^i · (1-t)^(n-i)）。
 *   优点：实现简洁，适合学习与中等次数（n ≤ ~30）。
 *   局限：高次时 t^i 与 (1-t)^(n-i) 各自可能下溢，且 pow 较慢；
 *        工业级实现会用 de Casteljau 或递推 B_{i,n} = (1-t)·B_{i,n-1} + t·B_{i-1,n-1}。
 *        本模块面向学习场景，不做此优化。
 *
 *   二项式系数 C(n,k)：用对称性 k = min(k, n-k) 后累乘，全程整数不溢出
 *   （n ≤ 30 时安全；n > 62 时 C(n,k) 可能溢出 int64，调用方需自行约束）。
 */
#pragma once

#include <cassert>
#include <cstdint>
#include <utility>

namespace mulan::math {

// ============================================================
// 二项式系数 C(n, k)
// ============================================================

/// 整数版二项式系数。k ∈ [0,n] 返回 C(n,k)；越界返回 0。
/// n ≤ 30 时结果在 int 范围内安全。
inline int binomial(int n, int k) noexcept {
    if (k < 0 || k > n)
        return 0;
    if (k == 0 || k == n)
        return 1;
    k = k < n - k ? k : n - k;  // 对称性：C(n,k) = C(n,n-k)

    int64_t result = 1;
    for (int i = 1; i <= k; ++i) {
        result = result * (n - k + i) / i;
    }
    // n ≤ 30 时 C(n,k) ≤ C(30,15) ≈ 1.55e8，int 安全
    return static_cast<int>(result);
}

// ============================================================
// Bernstein 多项式 B_{i,n}(t)
// ============================================================

/// 计算 B_{i,n}(t) = C(n,i) · t^i · (1-t)^(n-i)。
/// i ∉ [0,n] 时返回 0（曲线求值时 i 越界安全降级）。
/// 注意：t 不做 clamp，调用方负责约束 t ∈ [0,1]（基函数本身在域外也有定义，
///       但 Bezier 语义只关心 [0,1]）。
inline double bernstein(int i, int n, double t) noexcept {
    if (i < 0 || i > n)
        return 0.0;

    // 边界：t = 0 或 1 时 pow(0,0) 在标准库返回 1，恰好符合
    // B_{0,n}(0)=1, B_{i>0,n}(0)=0, B_{n,n}(1)=1 等
    double ti = 1.0;
    for (int e = 0; e < i; ++e)
        ti *= t;  // t^i（避免 pow 的浮点误差）
    double mt = 1.0 - t;
    double mt1 = 1.0;
    for (int e = 0; e < n - i; ++e)
        mt1 *= mt;  // (1-t)^(n-i)

    return binomial(n, i) * ti * mt1;
}

}  // namespace mulan::math
