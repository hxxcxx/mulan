/**
 * @file Tolerance.h
 * @brief 容差常量与近似比较函数
 *
 * 基于 truck-base::tolerance。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "Types.h"
#include <glm/gtx/norm.hpp>
#include <cmath>

namespace mulan::geometry {

/// 一般容差
inline constexpr double TOLERANCE = 1.0e-10;

/// 平方容差
inline constexpr double TOLERANCE2 = TOLERANCE * TOLERANCE;

/// 圆周率
inline constexpr double PI = 3.14159265358979323846;

/// 2π
inline constexpr double TWO_PI = 2.0 * PI;

/// 判断两个标量是否在容差范围内相等
inline bool near(double a, double b, double tol = TOLERANCE) {
    return std::abs(a - b) < tol;
}

/// 判断两个标量是否在平方容差范围内相等
inline bool near2(double a, double b, double tol = TOLERANCE2) {
    return std::abs(a - b) < tol;
}

/// 判断两个 2D 向量是否在容差范围内相等
inline bool near(const Vector2& a, const Vector2& b, double tol = TOLERANCE) {
    return glm::length2(a - b) < tol * tol;
}

/// 判断两个 3D 向量是否在容差范围内相等
inline bool near(const Vector3& a, const Vector3& b, double tol = TOLERANCE) {
    return glm::length2(a - b) < tol * tol;
}

/// 判断两个 4D 向量是否在容差范围内相等
inline bool near(const Vector4& a, const Vector4& b, double tol = TOLERANCE) {
    return glm::length2(a - b) < tol * tol;
}

/// 判断标量是否接近零
inline bool soSmall(double x, double tol = TOLERANCE) {
    return std::abs(x) < tol;
}

/// 安全的求逆，若为零则返回 0
inline double inv_or_zero(double x) {
    return soSmall(x) ? 0.0 : 1.0 / x;
}

/// 参数搜索相关常量
inline constexpr size_t SEARCH_PARAMETER_TRIALS = 100;
inline constexpr size_t PRESEARCH_DIVISION = 50;

} // namespace mulan::Geometry
