/**
 * @file mat2.h
 * @brief 2x2 矩阵（列主序）— 2D 线性变换
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 存储为列主序：m[col][row]。
 */
#pragma once

#include "linalg/vec2.h"

#include <cmath>

namespace mulan::math {

template<typename T>
struct Mat2T {
    /// 两列，每列一个 Vec2
    Vec2T<T> cols[2]{ Vec2T<T>(T(1), T(0)), Vec2T<T>(T(0), T(1)) };

    constexpr Mat2T() = default;
    explicit constexpr Mat2T(T diagonal)
        : cols{Vec2T<T>(diagonal, T(0)), Vec2T<T>(T(0), diagonal)} {}
    constexpr Mat2T(const Vec2T<T>& c0, const Vec2T<T>& c1) : cols{c0, c1} {}

    Vec2T<T>&       operator[](int c)       { return cols[c]; }
    const Vec2T<T>& operator[](int c) const { return cols[c]; }

    static constexpr Mat2T identity() {
        return Mat2T(Vec2T<T>(T(1), T(0)), Vec2T<T>(T(0), T(1)));
    }

    /// 旋转（弧度）
    static Mat2T rotation(T rad) {
        T c = std::cos(rad), s = std::sin(rad);
        // 列主序：第一列 (c, s)，第二列 (-s, c)
        return Mat2T(Vec2T<T>(c, s), Vec2T<T>(-s, c));
    }

    static constexpr Mat2T scale(T sx, T sy) {
        return Mat2T(Vec2T<T>(sx, T(0)), Vec2T<T>(T(0), sy));
    }

    Mat2T transposed() const {
        return Mat2T(Vec2T<T>(cols[0].x, cols[1].x),
                     Vec2T<T>(cols[0].y, cols[1].y));
    }

    T determinant() const {
        return cols[0].x * cols[1].y - cols[1].x * cols[0].y;
    }

    Mat2T inverse() const {
        T det = determinant();
        if (det == T(0)) return identity();
        T inv = T(1) / det;
        return Mat2T(Vec2T<T>( cols[1].y * inv, -cols[0].y * inv),
                     Vec2T<T>(-cols[1].x * inv,  cols[0].x * inv));
    }
};

// ---------- 矩阵 × 向量 / 矩阵 × 矩阵 ----------

template<typename T>
constexpr Vec2T<T> operator*(const Mat2T<T>& m, const Vec2T<T>& v) {
    return Vec2T<T>(m[0].x * v.x + m[1].x * v.y,
                    m[0].y * v.x + m[1].y * v.y);
}

template<typename T>
constexpr Mat2T<T> operator*(const Mat2T<T>& a, const Mat2T<T>& b) {
    Mat2T<T> r;
    for (int c = 0; c < 2; ++c) {
        r[c] = a * b[c];  // 矩阵列 = a * b的第c列
    }
    return r;
}

// ---------- 别名 ----------
using Mat2  = Mat2T<double>;
using FMat2 = Mat2T<float>;

} // namespace mulan::math
