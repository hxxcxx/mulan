/**
 * @file KnotVec.cpp
 * @brief KnotVec B样条基函数计算实现
 *
 * 基于 truck-geometry::nurbs::knotVec 的 Cox-de Boor 算法。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#include "KnotVec.h"
#include "../Tolerance.h"
#include <cmath>

namespace MulanGeo::Geometry {

// ============================================================
// Cox-de Boor 递推算法
// ============================================================

BasisWindow KnotVec::bsplineBasisFunctions(size_t degree, size_t der_rank, double t) const {
    auto result = tryBsplineBasisFunctions(degree, der_rank, t);
    if (!result) {
        throw std::runtime_error("KnotVec::bsplineBasisFunctions: failed to compute basis functions");
    }
    return *result;
}

std::optional<BasisWindow> KnotVec::tryBsplineBasisFunctions(
    size_t degree, size_t der_rank, double t
) const {
    size_t n = knots_.size();
    size_t num_basis = n - degree - 1; // 控制点数

    if (n <= degree) return std::nullopt;
    if (soSmall(rangeLength())) return std::nullopt;

    // 找到 t 所在的节点区间
    size_t k = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (t >= knots_[i] - TOLERANCE && t < knots_[i + 1] + TOLERANCE) {
            k = i;
            break;
        }
    }
    // 处理 t 在最右端的情况
    if (t >= knots_.back() - TOLERANCE) {
        k = n - 2;
        // 找到最后一个非退化区间
        while (k > 0 && near(knots_[k], knots_[k + 1])) {
            --k;
        }
    }

    // 基函数窗口大小
    size_t width = std::min(degree + 1, num_basis > k ? num_basis - (k > degree ? k - degree : 0) : degree + 1);
    size_t base = k > degree ? k - degree : 0;
    // 调整 width 使得 base + width <= num_basis
    width = std::min(width, num_basis - base);

    // 阶段 0: 初始化 (piecewise linear hat functions)
    // N[i][0] = 1 if knots[i] <= t < knots[i+1], else 0
    std::vector<double> basis(width, 0.0);
    size_t local_idx = k - base;
    if (local_idx < width) {
        basis[local_idx] = 1.0;
    }

    // 阶段 p=1..degree: Cox-de Boor 递推
    for (size_t p = 1; p <= degree; ++p) {
        // 从右向左更新，避免覆盖
        for (int i = static_cast<int>(width) - 1; i >= 0; --i) {
            size_t knot_idx = base + static_cast<size_t>(i);

            double left_term = 0.0;
            if (knot_idx + p < n) {
                double denom = knots_[knot_idx + p] - knots_[knot_idx];
                if (!soSmall(denom)) {
                    left_term = (t - knots_[knot_idx]) / denom * basis[i];
                }
            }

            double right_term = 0.0;
            if (i + 1 < static_cast<int>(width) && knot_idx + 1 + p < n) {
                double denom = knots_[knot_idx + 1 + p] - knots_[knot_idx + 1];
                if (!soSmall(denom)) {
                    right_term = (knots_[knot_idx + 1 + p] - t) / denom * basis[i + 1];
                }
            }

            basis[i] = left_term + right_term;
        }
    }

    // 求导
    if (der_rank > 0) {
        // 存储各阶基函数用于求导
        // 使用递推: dN[i][p]/dt = p * (N[i][p-1]/(t[i+p]-t[i]) - N[i+1][p-1]/(t[i+p+1]-t[i+1]))
        // 这里使用简化方法: 重新计算带导数的基函数

        // 先保存 0 阶结果
        std::vector<std::vector<double>> all_basis(der_rank + 1);
        all_basis[0] = basis;

        // 重新从 0 阶开始，存储所有中间结果
        std::vector<double> current(width, 0.0);
        if (local_idx < width) current[local_idx] = 1.0;

        // 存储每个 degree 层级的结果
        std::vector<std::vector<double>> layers(degree + 1);
        layers[0] = current;

        for (size_t p = 1; p <= degree; ++p) {
            std::vector<double> next(width, 0.0);
            for (size_t i = 0; i < width; ++i) {
                size_t ki = base + i;
                double left_term = 0.0;
                if (ki + p < n) {
                    double denom = knots_[ki + p] - knots_[ki];
                    if (!soSmall(denom)) {
                        left_term = (t - knots_[ki]) / denom * layers[p - 1][i];
                    }
                }
                double right_term = 0.0;
                if (i + 1 < width && ki + 1 + p < n) {
                    double denom = knots_[ki + 1 + p] - knots_[ki + 1];
                    if (!soSmall(denom)) {
                        right_term = (knots_[ki + 1 + p] - t) / denom * layers[p - 1][i + 1];
                    }
                }
                next[i] = left_term + right_term;
            }
            layers[p] = next;
        }

        // 数值微分: 使用差分近似
        // 更精确的方法是用解析导数
        // N'[p][i] = p * (N[p-1][i] / (t[i+p] - t[i]) - N[p-1][i+1] / (t[i+p+1] - t[i+1]))
        basis = layers[degree];

        // 计算 der_rank 阶导数
        for (size_t d = 1; d <= der_rank; ++d) {
            size_t p_eff = degree - d + 1;
            std::vector<double> deriv(width, 0.0);
            for (size_t i = 0; i < width; ++i) {
                size_t ki = base + i;
                double left_term = 0.0;
                if (ki + p_eff < n) {
                    double denom = knots_[ki + p_eff] - knots_[ki];
                    if (!soSmall(denom)) {
                        left_term = static_cast<double>(p_eff) / denom * layers[p_eff - 1][i];
                    }
                }
                double right_term = 0.0;
                if (i + 1 < width && ki + 1 + p_eff < n) {
                    double denom = knots_[ki + 1 + p_eff] - knots_[ki + 1];
                    if (!soSmall(denom)) {
                        right_term = static_cast<double>(p_eff) / denom * layers[p_eff - 1][i + 1];
                    }
                }
                deriv[i] = left_term - right_term;
            }
            // 更新 layers 用于下一阶导数
            // 递归计算高阶导数时需要重新计算 layers
            basis = deriv;
        }
    }

    return BasisWindow(base, std::move(basis), num_basis);
}

} // namespace MulanGeo::Geometry
