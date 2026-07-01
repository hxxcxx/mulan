/**
 * @file vec2.h
 * @brief 2D 向量
 * @author hxxcxx
 * @date 2026-06-29
 */
#pragma once

#include "geo_math.h"
#include "tolerance.h"

#include <cmath>

namespace mulan::geo {

template<typename T>
struct Vec2T {
    T x{};
    T y{};

    // ---------- 构造 ----------
    constexpr Vec2T() = default;
    constexpr Vec2T(T x_, T y_) : x(x_), y(y_) {}

    // ---------- 下标 ----------
    T&       operator[](int i)       { return (i == 0) ? x : y; }
    const T& operator[](int i) const { return (i == 0) ? x : y; }

    // ---------- 算术赋值 ----------
    Vec2T& operator+=(const Vec2T& o) { x += o.x; y += o.y; return *this; }
    Vec2T& operator-=(const Vec2T& o) { x -= o.x; y -= o.y; return *this; }
    Vec2T& operator*=(T s)            { x *= s;   y *= s;   return *this; }
    Vec2T& operator/=(T s)            { x /= s;   y /= s;   return *this; }

    Vec2T operator-() const { return Vec2T(-x, -y); }

    // ---------- 几何查询 ----------
    T lengthSq() const { return x * x + y * y; }
    T length()   const { return std::sqrt(lengthSq()); }
    /// 平方长度（glm length2 等价）
    T length2()  const { return lengthSq(); }

    Vec2T normalized() const {
        T len = length();
        return (len > T(0)) ? Vec2T(x / len, y / len) : Vec2T{};
    }

    /// 点乘
    constexpr T dot(const Vec2T& o) const { return x * o.x + y * o.y; }
    /// 2D 叉乘（返回标量，z 分量）
    constexpr T cross(const Vec2T& o) const { return x * o.y - y * o.x; }
    /// 线性插值到 o（t∈[0,1]）
    constexpr Vec2T lerp(const Vec2T& o, T t) const {
        return Vec2T(x + (o.x - x) * t, y + (o.y - y) * t);
    }
    /// 到 o 的距离
    T distanceTo(const Vec2T& o) const { return (*this - o).length(); }
    /// 到 o 的平方距离（glm distance2 等价）
    T distanceSqTo(const Vec2T& o) const { return (*this - o).lengthSq(); }

    bool isZero(const Tolerance& tol = defaultTolerance()) const {
        return lengthSq() <= T(tol.lengthEps) * T(tol.lengthEps);
    }

    // ---------- 工厂 ----------
    static constexpr Vec2T zero()   { return Vec2T(T(0), T(0)); }
    static constexpr Vec2T unitX()  { return Vec2T(T(1), T(0)); }
    static constexpr Vec2T unitY()  { return Vec2T(T(0), T(1)); }
};

// ---------- 自由函数运算符 ----------

template<typename T>
constexpr Vec2T<T> operator+(const Vec2T<T>& a, const Vec2T<T>& b) { return Vec2T<T>(a.x + b.x, a.y + b.y); }
template<typename T>
constexpr Vec2T<T> operator-(const Vec2T<T>& a, const Vec2T<T>& b) { return Vec2T<T>(a.x - b.x, a.y - b.y); }
template<typename T>
constexpr Vec2T<T> operator*(const Vec2T<T>& a, T s) { return Vec2T<T>(a.x * s, a.y * s); }
template<typename T>
constexpr Vec2T<T> operator*(T s, const Vec2T<T>& a) { return a * s; }
template<typename T>
constexpr Vec2T<T> operator/(const Vec2T<T>& a, T s) { return Vec2T<T>(a.x / s, a.y / s); }

/// 逐分量乘（Hadamard 积）
template<typename T>
constexpr Vec2T<T> operator*(const Vec2T<T>& a, const Vec2T<T>& b) { return Vec2T<T>(a.x * b.x, a.y * b.y); }

template<typename T>
constexpr bool operator==(const Vec2T<T>& a, const Vec2T<T>& b) { return a.x == b.x && a.y == b.y; }
template<typename T>
constexpr bool operator!=(const Vec2T<T>& a, const Vec2T<T>& b) { return !(a == b); }

// ---------- 自由函数（转发到成员方法，便于对称调用）----------

template<typename T>
constexpr T dot(const Vec2T<T>& a, const Vec2T<T>& b) { return a.dot(b); }

/// 2D 叉乘，返回标量（z 分量）
template<typename T>
constexpr T cross(const Vec2T<T>& a, const Vec2T<T>& b) { return a.cross(b); }

template<typename T>
constexpr Vec2T<T> lerp(const Vec2T<T>& a, const Vec2T<T>& b, T t) { return a.lerp(b, t); }

template<typename T>
T distance(const Vec2T<T>& a, const Vec2T<T>& b) { return a.distanceTo(b); }
/// 平方长度（glm length2 等价）
template<typename T>
constexpr T length2(const Vec2T<T>& v) { return v.lengthSq(); }
/// 平方距离（glm distance2 等价）
template<typename T>
constexpr T distance2(const Vec2T<T>& a, const Vec2T<T>& b) { return a.distanceSqTo(b); }

// ---------- 别名 ----------
using Vec2  = Vec2T<double>;
using FVec2 = Vec2T<float>;

} // namespace mulan::geo
