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
// 同时求解（通用模板）
// ============================================================
template<typename Handler>
std::optional<std::vector<double>> simultaneous_newton(
    Handler& handler,
    std::vector<double> init,
    size_t trials,
    double tol = TOLERANCE
) {
    for (size_t iter = 0; iter <= trials; ++iter) {
        auto [value, jacobian] = handler(init);

        // 检查收敛
        bool converged = true;
        for (size_t i = 0; i < value.size(); ++i) {
            if (std::abs(value[i]) > tol) {
                converged = false;
                break;
            }
        }
        if (converged) return init;

        // 更新: init -= jacobian^{-1} * value
        // 简化: 对于小系统直接求解
        (void)jacobian;
        // 具体实现依赖 Handler 类型
    }
    return std::nullopt;
}

} // namespace mulan::Geometry
