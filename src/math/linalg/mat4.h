/**
 * @file mat4.h
 * @brief 4x4 矩阵（列主序）— 仿射变换 / 投影
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 列主序存储：m[col][row]，便于直接上传 shader uniform。
 * 仿射变换部分（平移/旋转/缩放）默认构造右手系视图投影。
 */
#pragma once

#include "mat3.h"
#include "vec3.h"
#include "vec4.h"

#include <cmath>

namespace mulan::geo {

template<typename T>
struct Mat4T {
    Vec4T<T> cols[4]{
        Vec4T<T>(T(1), T(0), T(0), T(0)),
        Vec4T<T>(T(0), T(1), T(0), T(0)),
        Vec4T<T>(T(0), T(0), T(1), T(0)),
        Vec4T<T>(T(0), T(0), T(0), T(1))
    };

    constexpr Mat4T() = default;
    explicit constexpr Mat4T(T diagonal)
        : cols{Vec4T<T>(diagonal, T(0), T(0), T(0)),
               Vec4T<T>(T(0), diagonal, T(0), T(0)),
               Vec4T<T>(T(0), T(0), diagonal, T(0)),
               Vec4T<T>(T(0), T(0), T(0), diagonal)} {}
    constexpr Mat4T(const Vec4T<T>& c0, const Vec4T<T>& c1,
                    const Vec4T<T>& c2, const Vec4T<T>& c3)
        : cols{c0, c1, c2, c3} {}

    explicit constexpr Mat4T(const Mat3T<T>& m)
        : cols{Vec4T<T>(m[0], T(0)),
               Vec4T<T>(m[1], T(0)),
               Vec4T<T>(m[2], T(0)),
               Vec4T<T>(T(0), T(0), T(0), T(1))} {}

    template<typename U>
    explicit constexpr Mat4T(const Mat4T<U>& m)
        : cols{Vec4T<T>(m[0]), Vec4T<T>(m[1]), Vec4T<T>(m[2]), Vec4T<T>(m[3])} {}

    Vec4T<T>&       operator[](int c)       { return cols[c]; }
    const Vec4T<T>& operator[](int c) const { return cols[c]; }

    /// 连续存储访问（列主序 16 元素，可直接喂 GPU/OCCT）。
    /// 依赖 Vec4T<T> 为紧凑 POD（4 个 T 连续无 padding）。
    T*       data()       { return &cols[0].x; }
    const T* data() const { return &cols[0].x; }

    static constexpr Mat4T identity() { return Mat4T{}; }

    // ---------- 仿射变换工厂 ----------

    static constexpr Mat4T translate(const Vec3T<T>& t) {
        return Mat4T(Vec4T<T>(T(1), T(0), T(0), T(0)),
                     Vec4T<T>(T(0), T(1), T(0), T(0)),
                     Vec4T<T>(T(0), T(0), T(1), T(0)),
                     Vec4T<T>(t.x,  t.y,  t.z,  T(1)));
    }

    /// 绕单位轴 axis 旋转 rad 弧度
    static Mat4T rotation(const Vec3T<T>& axis, T rad) {
        T c = std::cos(rad), s = std::sin(rad), t = T(1) - c;
        Vec3T<T> n = axis.normalizedOr(Vec3T<T>::unitZ());
        T x = n.x, y = n.y, z = n.z;
        return Mat4T(
            Vec4T<T>(t*x*x + c,   t*x*y + s*z, t*x*z - s*y, T(0)),
            Vec4T<T>(t*x*y - s*z, t*y*y + c,   t*y*z + s*x, T(0)),
            Vec4T<T>(t*x*z + s*y, t*y*z - s*x, t*z*z + c,   T(0)),
            Vec4T<T>(T(0), T(0), T(0), T(1)));
    }

    /// 绕 X/Y/Z 单轴旋转
    static Mat4T rotationX(T rad) {
        T c = std::cos(rad), s = std::sin(rad);
        return Mat4T(Vec4T<T>(T(1), T(0), T(0), T(0)),
                     Vec4T<T>(T(0), c,    s,    T(0)),
                     Vec4T<T>(T(0), -s,   c,    T(0)),
                     Vec4T<T>(T(0), T(0), T(0), T(1)));
    }
    static Mat4T rotationY(T rad) {
        T c = std::cos(rad), s = std::sin(rad);
        return Mat4T(Vec4T<T>(c,    T(0), -s,   T(0)),
                     Vec4T<T>(T(0), T(1), T(0), T(0)),
                     Vec4T<T>(s,    T(0), c,    T(0)),
                     Vec4T<T>(T(0), T(0), T(0), T(1)));
    }
    static Mat4T rotationZ(T rad) {
        T c = std::cos(rad), s = std::sin(rad);
        return Mat4T(Vec4T<T>(c,    s,    T(0), T(0)),
                     Vec4T<T>(-s,   c,    T(0), T(0)),
                     Vec4T<T>(T(0), T(0), T(1), T(0)),
                     Vec4T<T>(T(0), T(0), T(0), T(1)));
    }

    static constexpr Mat4T scale(const Vec3T<T>& s) {
        return Mat4T(Vec4T<T>(s.x,  T(0), T(0), T(0)),
                     Vec4T<T>(T(0), s.y,  T(0), T(0)),
                     Vec4T<T>(T(0), T(0), s.z,  T(0)),
                     Vec4T<T>(T(0), T(0), T(0), T(1)));
    }
    static constexpr Mat4T scale(T s) { return scale(Vec3T<T>(s, s, s)); }

    // ---------- 投影（右手系，OpenGL/Vulkan 风格）----------

    /// 透视投影。fovY 为竖直视野（弧度）。投影到 NDC z∈[-1,1]。
    static Mat4T perspective(T fovY, T aspect, T zNear, T zFar) {
        T f = T(1) / std::tan(fovY * T(0.5));
        T zRange = zNear - zFar;
        return Mat4T(
            Vec4T<T>(f / aspect, T(0), T(0),                       T(0)),
            Vec4T<T>(T(0),       f,    T(0),                       T(0)),
            Vec4T<T>(T(0),       T(0), (zFar + zNear) / zRange,   T(-1)),
            Vec4T<T>(T(0),       T(0), T(2) * zFar * zNear / zRange, T(0)));
    }

    /// 正交投影。投影到 NDC z∈[-1,1]。
    static Mat4T ortho(T left, T right, T bottom, T top, T zNear, T zFar) {
        T rl = T(1) / (right - left);
        T tb = T(1) / (top - bottom);
        T fn = T(1) / (zNear - zFar);
        return Mat4T(
            Vec4T<T>(T(2) * rl, T(0),    T(0),             T(0)),
            Vec4T<T>(T(0),      T(2) * tb, T(0),           T(0)),
            Vec4T<T>(T(0),      T(0),    T(2) * fn,        T(0)),
            Vec4T<T>(-(right + left) * rl, -(top + bottom) * tb, (zFar + zNear) * fn, T(1)));
    }

    /// 视图矩阵（右手系，相机看向 -Z）
    static Mat4T lookAt(const Vec3T<T>& eye, const Vec3T<T>& center, const Vec3T<T>& up) {
        Vec3T<T> f = (center - eye).normalized();
        Vec3T<T> s = f.cross(up).normalized();
        Vec3T<T> u = s.cross(f);
        return Mat4T(
            Vec4T<T>(s.x,  u.x,  -f.x, T(0)),
            Vec4T<T>(s.y,  u.y,  -f.y, T(0)),
            Vec4T<T>(s.z,  u.z,  -f.z, T(0)),
            Vec4T<T>(-s.dot(eye), -u.dot(eye), f.dot(eye), T(1)));
    }

    // ---------- 基本运算 ----------

    Mat4T transposed() const {
        const Mat4T& m = *this;
        return Mat4T(
            Vec4T<T>(m[0].x, m[1].x, m[2].x, m[3].x),
            Vec4T<T>(m[0].y, m[1].y, m[2].y, m[3].y),
            Vec4T<T>(m[0].z, m[1].z, m[2].z, m[3].z),
            Vec4T<T>(m[0].w, m[1].w, m[2].w, m[3].w));
    }

    /// 通用 4x4 求逆（克莱姆法则，对仿射/投影均正确）
    Mat4T inverse() const {
        const Mat4T& m = *this;
        T coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
        T coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
        T coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];
        T coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
        T coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
        T coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];
        T coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
        T coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
        T coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];
        T coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
        T coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
        T coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];
        T coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
        T coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
        T coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];
        T coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
        T coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
        T coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

        Vec4T<T> fac0(coef00, coef00, coef02, coef03);
        Vec4T<T> fac1(coef04, coef04, coef06, coef07);
        Vec4T<T> fac2(coef08, coef08, coef10, coef11);
        Vec4T<T> fac3(coef12, coef12, coef14, coef15);
        Vec4T<T> fac4(coef16, coef16, coef18, coef19);
        Vec4T<T> fac5(coef20, coef20, coef22, coef23);

        Vec4T<T> vec0(m[1][0], m[0][0], m[0][0], m[0][0]);
        Vec4T<T> vec1(m[1][1], m[0][1], m[0][1], m[0][1]);
        Vec4T<T> vec2(m[1][2], m[0][2], m[0][2], m[0][2]);
        Vec4T<T> vec3(m[1][3], m[0][3], m[0][3], m[0][3]);

        Vec4T<T> inv0(vec1 * fac0 - vec2 * fac1 + vec3 * fac2);
        Vec4T<T> inv1(vec0 * fac0 - vec2 * fac3 + vec3 * fac4);
        Vec4T<T> inv2(vec0 * fac1 - vec1 * fac3 + vec3 * fac5);
        Vec4T<T> inv3(vec0 * fac2 - vec1 * fac4 + vec2 * fac5);

        Vec4T<T> signA(T(1), T(-1), T(1), T(-1));
        Vec4T<T> signB(T(-1), T(1), T(-1), T(1));
        inv0 = inv0 * signA;
        inv1 = inv1 * signB;
        inv2 = inv2 * signA;
        inv3 = inv3 * signB;

        // 行列式 = dot(m 第0行, inv0)
        T det = m[0][0] * inv0[0] + m[0][1] * inv1[0] + m[0][2] * inv2[0] + m[0][3] * inv3[0];
        if (det == T(0)) return identity();
        T invDet = T(1) / det;

        return Mat4T(inv0 * invDet, inv1 * invDet, inv2 * invDet, inv3 * invDet);
    }
};

// ---------- 矩阵 × 向量 / 矩阵 × 矩阵 ----------

template<typename T>
constexpr Vec4T<T> operator*(const Mat4T<T>& m, const Vec4T<T>& v) {
    return Vec4T<T>(
        m[0].x * v.x + m[1].x * v.y + m[2].x * v.z + m[3].x * v.w,
        m[0].y * v.x + m[1].y * v.y + m[2].y * v.z + m[3].y * v.w,
        m[0].z * v.x + m[1].z * v.y + m[2].z * v.z + m[3].z * v.w,
        m[0].w * v.x + m[1].w * v.y + m[2].w * v.z + m[3].w * v.w);
}

/// 仿射变换点（w=1），返回 Vec3
template<typename T>
Vec3T<T> transformPoint(const Mat4T<T>& m, const Vec3T<T>& p) {
    Vec4T<T> r = m * Vec4T<T>(p.x, p.y, p.z, T(1));
    return Vec3T<T>(r.x, r.y, r.z);
}

/// 仿射变换方向（w=0，不受平移影响），返回 Vec3
template<typename T>
Vec3T<T> transformDir(const Mat4T<T>& m, const Vec3T<T>& d) {
    Vec4T<T> r = m * Vec4T<T>(d.x, d.y, d.z, T(0));
    return Vec3T<T>(r.x, r.y, r.z);
}

template<typename T>
constexpr Mat4T<T> operator*(const Mat4T<T>& a, const Mat4T<T>& b) {
    Mat4T<T> r;
    for (int c = 0; c < 4; ++c) r[c] = a * b[c];
    return r;
}

// ---------- 别名 ----------
using Mat4  = Mat4T<double>;
using FMat4 = Mat4T<float>;

template<typename T>
template<typename U>
constexpr Mat3T<T>::Mat3T(const Mat4T<U>& m)
    : cols{Vec3T<T>(m[0].x, m[0].y, m[0].z),
           Vec3T<T>(m[1].x, m[1].y, m[1].z),
           Vec3T<T>(m[2].x, m[2].y, m[2].z)} {}

template<typename T>
inline Mat4T<T> translate(const Mat4T<T>& m, const Vec3T<T>& t) {
    return m * Mat4T<T>::translate(t);
}
template<typename T>
inline Mat4T<T> scale(const Mat4T<T>& m, const Vec3T<T>& s) {
    return m * Mat4T<T>::scale(s);
}
template<typename T>
inline Mat4T<T> perspective(T fovY, T aspect, T zNear, T zFar) {
    return Mat4T<T>::perspective(fovY, aspect, zNear, zFar);
}
template<typename T>
inline Mat4T<T> ortho(T left, T right, T bottom, T top, T zNear, T zFar) {
    return Mat4T<T>::ortho(left, right, bottom, top, zNear, zFar);
}

// 保证 Vec4T 紧凑、Mat4T 为 16 元素连续存储（GPU/OCCT 边界契约）
static_assert(sizeof(FMat4) == 16 * sizeof(float),  "FMat4 must be 16 contiguous floats");
static_assert(sizeof(Mat4)  == 16 * sizeof(double), "Mat4 must be 16 contiguous doubles");

} // namespace mulan::geo
