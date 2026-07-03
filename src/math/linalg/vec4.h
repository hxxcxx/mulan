/**
 * @file vec4.h
 * @brief 4D 向量（齐次坐标）
 * @author hxxcxx
 * @date 2026-06-29
 */
#pragma once

#include "geo_math.h"
#include "vec2.h"
#include "vec3.h"

#include <cmath>

namespace mulan::geo {

template<typename T>
struct Vec4T {
    T x{};
    T y{};
    T z{};
    T w{};

    constexpr Vec4T() = default;
    explicit constexpr Vec4T(T v) : x(v), y(v), z(v), w(v) {}
    constexpr Vec4T(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4T(const Vec2T<T>& xy, T z_, T w_) : x(xy.x), y(xy.y), z(z_), w(w_) {}
    constexpr Vec4T(const Vec3T<T>& xyz, T w_) : x(xyz.x), y(xyz.y), z(xyz.z), w(w_) {}

    template<typename U>
    explicit constexpr Vec4T(const Vec4T<U>& v)
        : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)),
          z(static_cast<T>(v.z)), w(static_cast<T>(v.w)) {}

    T&       operator[](int i)       { return data()[i]; }
    const T& operator[](int i) const { return data()[i]; }

    Vec4T& operator+=(const Vec4T& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    Vec4T& operator-=(const Vec4T& o) { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
    Vec4T& operator*=(T s)            { x *= s; y *= s; z *= s; w *= s; return *this; }
    Vec4T& operator/=(T s)            { x /= s; y /= s; z /= s; w /= s; return *this; }

    Vec4T operator-() const { return Vec4T(-x, -y, -z, -w); }

    T lengthSq() const { return x * x + y * y + z * z + w * w; }
    T length()   const { return std::sqrt(lengthSq()); }
    /// 平方长度（glm length2 等价）
    T length2()  const { return lengthSq(); }

    /// 点乘
    constexpr T dot(const Vec4T& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    constexpr Vec3T<T> xyz() const { return Vec3T<T>(x, y, z); }

private:
    T*       data()       { return &x; }
    const T* data() const { return &x; }
};

template<typename T>
constexpr Vec4T<T> operator+(const Vec4T<T>& a, const Vec4T<T>& b) {
    return Vec4T<T>(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
template<typename T>
constexpr Vec4T<T> operator-(const Vec4T<T>& a, const Vec4T<T>& b) {
    return Vec4T<T>(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}
template<typename T>
constexpr Vec4T<T> operator*(const Vec4T<T>& a, T s) { return Vec4T<T>(a.x * s, a.y * s, a.z * s, a.w * s); }
template<typename T>
constexpr Vec4T<T> operator*(T s, const Vec4T<T>& a) { return a * s; }
template<typename T>
constexpr Vec4T<T> operator/(const Vec4T<T>& a, T s) { return Vec4T<T>(a.x / s, a.y / s, a.z / s, a.w / s); }

/// 逐分量乘（Hadamard 积）
template<typename T>
constexpr Vec4T<T> operator*(const Vec4T<T>& a, const Vec4T<T>& b) {
    return Vec4T<T>(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}

template<typename T>
constexpr bool operator==(const Vec4T<T>& a, const Vec4T<T>& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
template<typename T>
constexpr bool operator!=(const Vec4T<T>& a, const Vec4T<T>& b) { return !(a == b); }

template<typename T>
template<typename U>
constexpr Vec3T<T>::Vec3T(const Vec4T<U>& v)
    : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)) {}

// ---------- 别名 ----------
using Vec4  = Vec4T<double>;
using FVec4 = Vec4T<float>;

} // namespace mulan::geo
