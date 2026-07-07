/**
 * @file bspline_basis.h
 * @brief B-spline 基函数 — Cox-de Boor 递归 + 节点区间查找 + 节点向量生成
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义（The NURBS Book §2.x，下文记号 NURBS Book = Piegl & Tiller）：
 *   节点向量 U = {u_0, u_1, ..., u_m}，非递减。
 *   第 i 个 p 次 B-spline 基函数：
 *     N_{i,0}(u) = 1   若 u_i ≤ u < u_{i+1}
 *                = 0   否则
 *     N_{i,p}(u) = (u-u_i)/(u_{i+p}-u_i) · N_{i,p-1}(u)
 *                + (u_{i+p+1}-u)/(u_{i+p+1}-u_{i+1}) · N_{i+1,p-1}(u)
 *   分母为 0 时该系数约定为 0（支撑区退化为点）。
 *
 * 节点区间查找（NURBS Book A2.1 FindSpan）：
 *   给定参数 u，返回 k 使得 u_k ≤ u < u_{k+1}（即 u 落在第 k 个节点区间内）。
 *   本实现用线性查找（控制点规模通常不大，分支预测友好；工业级可换二分）。
 *
 * 设计：
 *   - 本头文件只提供【标量】基函数工具，不依赖 Point/Vec。曲线/曲面各自实现
 *     de Boor / de Casteljau 类算法时直接复用这里的 span 查找与基函数求值。
 *   - 容差：分母判零用全局 paramEps（1e-10），与 tolerance.h 一致。
 *
 * 边界：
 *   - 节点向量必须非递减（调用方保证，本文件不校验）。
 *   - 求 span 时 u 自动 clamp 到有效定义域 [u_p, u_{m-p}]。
 *   - 右端点闭合约定：clamped 节点向量中 u = u_max 时 NURBS Book 把 span 调整为
 *     n（最后一个有效区间），本 findSpan 也做此处理，使 evaluate(u_max) 落在
 *     最后 p+1 个控制点上。
 */
#pragma once

#include "../math_export.h"

#include "../scalar/tolerance.h"

#include <vector>

namespace mulan::math {

// ============================================================
// 节点区间查找 —— NURBS Book A2.1
// ============================================================

/// 返回 k 使 U[k] ≤ u < U[k+1]，定义域外 clamp 到 [p, n]。
/// n = 控制点数 - 1，p = 次数。U 长度 = n + p + 2。
/// 特殊处理 u == U.back()（右端点）：返回 n（最后有效 span）。
MATH_API int bsplineFindSpan(int n, int p, double u, const std::vector<double>& U) noexcept;

// ============================================================
// Cox-de Boor 基函数求值 —— NURBS Book A2.2（单点单基函数）
// ============================================================

/// 计算 N_{i,p}(u)。分母为 0 时该系数按 0 处理（容差 paramEps）。
/// 越界 i 返回 0。学习/参考用；批量求一整组基函数建议用 bsplineBasisFunctions。
MATH_API double bsplineBasis(int i, int p, double u, const std::vector<double>& U) noexcept;

/// 一次求出 span k 上 p+1 个非零基函数 N_{k-p+0..p,p}(u)。NURBS Book A2.2。
/// 返回数组 N[0..p]，N[r] = N_{k-p+r, p}(u)。比逐个调用 bsplineBasis 高效（共享子问题）。
MATH_API std::vector<double> bsplineBasisFunctions(int p, int k, double u, const std::vector<double>& U);

// ============================================================
// 节点向量生成
// ============================================================

/// 生成 clamped（开）均匀节点向量：首尾各 p+1 个重复，内部均匀分布。
/// 总长度 = controlPointCount + degree + 1。曲线穿过首末控制点。
/// 前置条件：controlPointCount > degree（否则曲线退化）。
MATH_API std::vector<double> clampedKnotVector(int degree, int controlPointCount);

/// 生成 uniform（均匀）节点向量：从 0 开始等距分布。
/// 注意：uniform 节点向量下曲线【不】穿过首末控制点（首末段定义域非 [0,1]）。
MATH_API std::vector<double> uniformKnotVector(int degree, int controlPointCount);

}  // namespace mulan::math
