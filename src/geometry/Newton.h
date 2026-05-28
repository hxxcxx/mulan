/**
 * @file Newton.h
 * @brief Newton 迭代求解器
 *
 * 基于 truck-base::newton。
 * 支持标量、2D、3D、4D Newton 法求解非线性方程组。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "Types.h"
#include "Tolerance.h"
#include "Export.h"
#include <vector>
#include <optional>
#include <functional>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace mulan::geometry {

// ============================================================
// Newton 求解器输出
// ============================================================
template<typename V, typename M>
struct CalcOutput {
    V value;       // 函数值
    M derivation;  // Jacobian
};

// ============================================================
// Newton 求解日志
// ============================================================
template<typename V>
struct NewtonLog {
    std::vector<V> log;
    bool degenerate = false;
    bool valid = false;
};

// ============================================================
// 标量 Newton 法
// ============================================================
inline std::optional<double> newton_solve_scalar(
    std::function<CalcOutput<double, double>(double)> func,
    double hint,
    size_t trials
) {
    for (size_t i = 0; i <= trials; ++i) {
        auto [value, deriv] = func(hint);
        if (soSmall(deriv)) return std::nullopt;
        double next = hint - value / deriv;
        if (near2(hint, next)) return hint;
        hint = next;
    }
    return std::nullopt;
}

// ============================================================
// 1D Newton 法（求解 f(t) = 0）
// ============================================================
std::optional<double> newton_solve1d(
    std::function<std::pair<double, double>(double)> func, // returns (value, derivative)
    double hint,
    size_t trials
);

// ============================================================
// 2D Newton 法（求解 f(u,v) = 0）
// ============================================================
std::optional<std::pair<double, double>> newton_solve2d(
    std::function<std::pair<Vector2, glm::dmat2>(const Vector2&)> func,
    const Vector2& hint,
    size_t trials
);

// ============================================================
// 3D Newton 法
// ============================================================
std::optional<Vector3> newton_solve3d(
    std::function<std::pair<Vector3, glm::dmat3>(const Vector3&)> func,
    const Vector3& hint,
    size_t trials
);

// ============================================================
// N×N 线性方程组求解 (部分主元高斯消元)
// ============================================================

namespace detail {

/// 求解 A * x = b，其中 A 是 n×n 矩阵（行优先存储）
/// @return x 向量，若矩阵奇异则返回空 vector
inline std::vector<double> solve_linear_system(
    std::vector<double> A,
    std::vector<double> b,
    size_t n
) {
    // 前向消元（部分主元选取）
    for (size_t col = 0; col < n; ++col) {
        // 寻找当前列中绝对值最大的行
        size_t maxRow = col;
        double maxVal = std::abs(A[col * n + col]);
        for (size_t row = col + 1; row < n; ++row) {
            double val = std::abs(A[row * n + col]);
            if (val > maxVal) {
                maxVal = val;
                maxRow = row;
            }
        }

        if (maxVal < 1e-15) {
            return {}; // 奇异矩阵
        }

        // 交换行
        if (maxRow != col) {
            for (size_t j = 0; j < n; ++j) {
                std::swap(A[col * n + j], A[maxRow * n + j]);
            }
            std::swap(b[col], b[maxRow]);
        }

        // 消元
        double pivot = A[col * n + col];
        for (size_t row = col + 1; row < n; ++row) {
            double factor = A[row * n + col] / pivot;
            for (size_t j = col + 1; j < n; ++j) {
                A[row * n + j] -= factor * A[col * n + j];
            }
            A[row * n + col] = 0.0;
            b[row] -= factor * b[col];
        }
    }

    // 回代
    std::vector<double> x(n, 0.0);
    for (size_t i = n; i-- > 0; ) {
        double sum = b[i];
        for (size_t j = i + 1; j < n; ++j) {
            sum -= A[i * n + j] * x[j];
        }
        if (std::abs(A[i * n + i]) < 1e-15) {
            return {}; // 奇异
        }
        x[i] = sum / A[i * n + i];
    }

    return x;
}

} // namespace detail

// ============================================================
// 同时求解（通用模板）
//
// Handler 需满足:
//   auto operator()(const std::vector<double>& x)
//       -> std::pair<std::vector<double>, std::vector<double>>
//   返回 (value, jacobian)
//   value: n 维函数值
//   jacobian: n×n Jacobian 矩阵（行优先存储）
// ============================================================
template<typename Handler>
std::optional<std::vector<double>> simultaneous_newton(
    Handler& handler,
    std::vector<double> init,
    size_t trials,
    double tol = TOLERANCE
) {
    size_t n = init.size();
    if (n == 0) return init;

    for (size_t iter = 0; iter <= trials; ++iter) {
        auto [value, jacobian] = handler(init);

        if (value.size() != n || jacobian.size() != n * n) {
            return std::nullopt;
        }

        // 检查收敛
        double maxVal = 0.0;
        for (size_t i = 0; i < n; ++i) {
            maxVal = std::max(maxVal, std::abs(value[i]));
        }
        if (maxVal < tol) return init;

        // 求解 J * delta = value
        auto delta = detail::solve_linear_system(jacobian, value, n);
        if (delta.empty()) return std::nullopt; // 奇异 Jacobian

        // 更新: x_{k+1} = x_k - J^{-1} * F(x_k)
        for (size_t i = 0; i < n; ++i) {
            init[i] -= delta[i];
        }
    }
    return std::nullopt;
}

} // namespace mulan::geometry
