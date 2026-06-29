/**
 * @file Vec3.h
 * @brief 3D 向量
 * @author hxxcxx
 * @date 2026-06-29
 */
#pragma once

#include "GeoMath.h"
#include "Tolerance.h"

#include <cmath>

namespace mulan::geo {

// 前向声明（用于变换成员方法声明；定义见 Point.h，那里能见到完整 Mat4T）
template<typename T> struct Mat4T;

template<typename T>
struct Vec3T {
    T x{};
    T y{};
    T z{};

    // ---------- 构造 ----------
    constexpr Vec3T() = default;
    constexpr Vec3T(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    // ---------- 下标 ----------
    T&       operator[](int i)       { return data()[i]; }
    const T& operator[](int i) const { return data()[i]; }

    // ---------- 算术赋值 ----------
    Vec3T& operator+=(const Vec3T& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3T& operator-=(const Vec3T& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3T& operator*=(T s)            { x *= s;   y *= s;   z *= s;   return *this; }
    Vec3T& operator/=(T s)            { x /= s;   y /= s;   z /= s;   return *this; }

    Vec3T operator-() const { return Vec3T(-x, -y, -z); }

    // ---------- 几何查询 ----------
    T lengthSq() const { return x * x + y * y + z * z; }
    T length()   const { return std::sqrt(lengthSq()); }

    Vec3T normalized() const {
        T len = length();
        return (len > T(0)) ? Vec3T(x / len, y / len, z / len) : Vec3T{};
    }

    /// 点乘
    constexpr T dot(const Vec3T& o) const { return x * o.x + y * o.y + z * o.z; }
    /// 叉乘
    constexpr Vec3T cross(const Vec3T& o) const {
        return Vec3T(y * o.z - z * o.y,
                     z * o.x - x * o.z,
                     x * o.y - y * o.x);
    }
    /// 线性插值到 o（t∈[0,1]）
    constexpr Vec3T lerp(const Vec3T& o, T t) const {
        return Vec3T(x + (o.x - x) * t,
                     y + (o.y - y) * t,
                     z + (o.z - z) * t);
    }
    /// 到 o 的距离
    T distanceTo(const Vec3T& o) const { return (*this - o).length(); }

    bool isZero(const Tolerance& tol = defaultTolerance()) const {
        return lengthSq() <= T(tol.lengthEps) * T(tol.lengthEps);
    }

    // ---------- 矩阵变换（成员声明；定义见文件末尾 / Point.h）----------

    /// 作为方向变换（w=0，忽略平移）。定义见 Point.h。
    template<typename U> Vec3T transformedAsDir(const Mat4T<U>& m) const;
    /// 作为法向变换（逆转置的左上 3x3）。定义见 Point.h。
    template<typename U> Vec3T transformedAsNormal(const Mat4T<U>& m) const;

    // ---------- 工厂 ----------
    static constexpr Vec3T zero()  { return Vec3T(T(0), T(0), T(0)); }
    static constexpr Vec3T unitX() { return Vec3T(T(1), T(0), T(0)); }
    static constexpr Vec3T unitY() { return Vec3T(T(0), T(1), T(0)); }
    static constexpr Vec3T unitZ() { return Vec3T(T(0), T(0), T(1)); }

private:
    /// 以数组视图访问（用于 operator[]，避免 UB 的 reinterpret）
    T*       data()       { return &x; }
    const T* data() const { return &x; }
};

// ---------- 自由函数运算符 ----------

template<typename T>
constexpr Vec3T<T> operator+(const Vec3T<T>& a, const Vec3T<T>& b) {
    return Vec3T<T>(a.x + b.x, a.y + b.y, a.z + b.z);
}
template<typename T>
constexpr Vec3T<T> operator-(const Vec3T<T>& a, const Vec3T<T>& b) {
    return Vec3T<T>(a.x - b.x, a.y - b.y, a.z - b.z);
}
template<typename T>
constexpr Vec3T<T> operator*(const Vec3T<T>& a, T s) { return Vec3T<T>(a.x * s, a.y * s, a.z * s); }
template<typename T>
constexpr Vec3T<T> operator*(T s, const Vec3T<T>& a) { return a * s; }
template<typename T>
constexpr Vec3T<T> operator/(const Vec3T<T>& a, T s) { return Vec3T<T>(a.x / s, a.y / s, a.z / s); }

/// 逐分量乘（Hadamard 积）
template<typename T>
constexpr Vec3T<T> operator*(const Vec3T<T>& a, const Vec3T<T>& b) {
    return Vec3T<T>(a.x * b.x, a.y * b.y, a.z * b.z);
}

template<typename T>
constexpr bool operator==(const Vec3T<T>& a, const Vec3T<T>& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
template<typename T>
constexpr bool operator!=(const Vec3T<T>& a, const Vec3T<T>& b) { return !(a == b); }

// ---------- 自由函数（转发到成员方法，便于对称调用）----------

template<typename T>
constexpr T dot(const Vec3T<T>& a, const Vec3T<T>& b) { return a.dot(b); }
template<typename T>
constexpr Vec3T<T> cross(const Vec3T<T>& a, const Vec3T<T>& b) { return a.cross(b); }
template<typename T>
constexpr Vec3T<T> lerp(const Vec3T<T>& a, const Vec3T<T>& b, T t) { return a.lerp(b, t); }
template<typename T>
T distance(const Vec3T<T>& a, const Vec3T<T>& b) { return a.distanceTo(b); }

// ---------- 别名 ----------
using Vec3  = Vec3T<double>;
using FVec3 = Vec3T<float>;

} // namespace mulan::geo
