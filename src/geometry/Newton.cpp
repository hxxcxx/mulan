/**
 * @file Newton.cpp
 * @brief Newton 求解器实现
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#include "Newton.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

namespace mulan::geometry {

std::optional<double> newton_solve1d(
    std::function<std::pair<double, double>(double)> func,
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

std::optional<std::pair<double, double>> newton_solve2d(
    std::function<std::pair<Vector2, glm::dmat2>(const Vector2&)> func,
    const Vector2& hint,
    size_t trials
) {
    Vector2 current = hint;
    for (size_t i = 0; i <= trials; ++i) {
        auto [value, jacobian] = func(current);
        auto inv = glm::inverse(jacobian);
        if (glm::isnan(inv[0][0])) return std::nullopt;
        Vector2 next = current - inv * value;
        if (glm::length2(next - current) < TOLERANCE2) {
            return std::make_pair(next.x, next.y);
        }
        current = next;
    }
    return std::nullopt;
}

std::optional<Vector3> newton_solve3d(
    std::function<std::pair<Vector3, glm::dmat3>(const Vector3&)> func,
    const Vector3& hint,
    size_t trials
) {
    Vector3 current = hint;
    for (size_t i = 0; i <= trials; ++i) {
        auto [value, jacobian] = func(current);
        auto inv = glm::inverse(jacobian);
        if (glm::isnan(inv[0][0])) return std::nullopt;
        Vector3 next = current - inv * value;
        if (glm::length2(next - current) < TOLERANCE2) {
            return next;
        }
        current = next;
    }
    return std::nullopt;
}

} // namespace mulan::geometry
