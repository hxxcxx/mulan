/**
 * @file geo_math.h
 * @brief 标量数学函数 — 基于 <cmath>/<algorithm>
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 供 geo 模块所有类型及外部共用。
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace mulan::geo {

// ============================================================
// 角度转换
// ============================================================

template<typename T>
constexpr T radians(T deg) { return deg * T(0.017453292519943295769); } // * pi/180

template<typename T>
constexpr T degrees(T rad) { return rad * T(57.29577951308232087679); } // * 180/pi

constexpr double kPi  = 3.14159265358979323846;
constexpr double kPi2 = 6.28318530717958647692;   // 2*pi
constexpr double kHalfPi = 1.57079632679489661923; // pi/2

// ============================================================
// clamp / lerp / min / max / abs
// ============================================================

template<typename T>
constexpr T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

template<typename T>
constexpr T lerp(T a, T b, T t) {
    return a + (b - a) * t;
}

template<typename T>
constexpr const T& min(const T& a, const T& b) { return b < a ? b : a; }

template<typename T>
constexpr const T& max(const T& a, const T& b) { return b < a ? a : b; }

template<typename T>
constexpr T abs(T v) { return v < T(0) ? -v : v; }

// ============================================================
// 平方/开方/幂（转发 std，便于 ADL/统一入口）
// ============================================================

inline double sqrt(double v)  { return std::sqrt(v); }
inline float  sqrt(float v)   { return std::sqrt(v); }
inline double pow(double b, double e) { return std::pow(b, e); }

} // namespace mulan::geo
