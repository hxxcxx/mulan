/**
 * @file mat3.h
 * @brief 3x3 矩阵（列主序）— 旋转 / 线性变换
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 列主序存储：m[col][row]，与图形学惯例一致。
 */
#pragma once

#include "vec3.h"

#include <cmath>

namespace mulan::geo {

template<typename T>
struct Mat3T {
    Vec3T<T> cols[3]{
        Vec3T<T>(T(1), T(0), T(0)),
        Vec3T<T>(T(0), T(1), T(0)),
        Vec3T<T>(T(0), T(0), T(1))
    };

    constexpr Mat3T() = default;
    constexpr Mat3T(const Vec3T<T>& c0, const Vec3T<T>& c1, const Vec3T<T>& c2)
        : cols{c0, c1, c2} {}

    Vec3T<T>&       operator[](int c)       { return cols[c]; }
    const Vec3T<T>& operator[](int c) const { return cols[c]; }

    static constexpr Mat3T identity() { return Mat3T{}; }

    /// 绕 axis（单位向量）旋转 rad 弧度（罗德里格斯公式）
    static Mat3T rotation(const Vec3T<T>& axis, T rad) {
        T c = std::cos(rad), s = std::sin(rad), t = T(1) - c;
        T x = axis.x, y = axis.y, z = axis.z;
        // 列主序
        return Mat3T(
            Vec3T<T>(t*x*x + c,   t*x*y + s*z, t*x*z - s*y),
            Vec3T<T>(t*x*y - s*z, t*y*y + c,   t*y*z + s*x),
            Vec3T<T>(t*x*z + s*y, t*y*z - s*x, t*z*z + c));
    }

    /// 欧拉角（绕 Z、Y、X 顺序），单位弧度
    static Mat3T rotationZYX(T rz, T ry, T rx) {
        return rotation(Vec3T<T>(T(0), T(0), T(1)), rz)
             * rotation(Vec3T<T>(T(0), T(1), T(0)), ry)
             * rotation(Vec3T<T>(T(1), T(0), T(0)), rx);
    }

    static constexpr Mat3T scale(T sx, T sy, T sz) {
        return Mat3T(Vec3T<T>(sx, T(0), T(0)),
                     Vec3T<T>(T(0), sy, T(0)),
                     Vec3T<T>(T(0), T(0), sz));
    }
    static constexpr Mat3T scale(const Vec3T<T>& s) { return scale(s.x, s.y, s.z); }

    Mat3T transposed() const {
        return Mat3T(Vec3T<T>(cols[0].x, cols[1].x, cols[2].x),
                     Vec3T<T>(cols[0].y, cols[1].y, cols[2].y),
                     Vec3T<T>(cols[0].z, cols[1].z, cols[2].z));
    }

    T determinant() const {
        const auto& a = cols[0]; const auto& b = cols[1]; const auto& c = cols[2];
        return a.x * (b.y * c.z - b.z * c.y)
             - a.y * (b.x * c.z - b.z * c.x)
             + a.z * (b.x * c.y - b.y * c.x);
    }

    Mat3T inverse() const {
        T det = determinant();
        if (det == T(0)) return identity();
        T inv = T(1) / det;
        const auto& a = cols[0]; const auto& b = cols[1]; const auto& c = cols[2];
        return Mat3T(
            Vec3T<T>((b.y * c.z - b.z * c.y) * inv,
                     (a.z * c.y - a.y * c.z) * inv,
                     (a.y * b.z - a.z * b.y) * inv),
            Vec3T<T>((b.z * c.x - b.x * c.z) * inv,
                     (a.x * c.z - a.z * c.x) * inv,
                     (a.z * b.x - a.x * b.z) * inv),
            Vec3T<T>((b.x * c.y - b.y * c.x) * inv,
                     (a.y * c.x - a.x * c.y) * inv,
                     (a.x * b.y - a.y * b.x) * inv));
    }
};

template<typename T>
constexpr Vec3T<T> operator*(const Mat3T<T>& m, const Vec3T<T>& v) {
    return Vec3T<T>(m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
                    m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
                    m[0].z * v.x + m[1].z * v.y + m[2].z * v.z);
}

template<typename T>
constexpr Mat3T<T> operator*(const Mat3T<T>& a, const Mat3T<T>& b) {
    Mat3T<T> r;
    for (int c = 0; c < 3; ++c) r[c] = a * b[c];
    return r;
}

// ---------- 别名 ----------
using Mat3  = Mat3T<double>;
using FMat3 = Mat3T<float>;

} // namespace mulan::geo
