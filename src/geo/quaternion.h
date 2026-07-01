/**
 * @file quaternion.h
 * @brief 四元数 — 旋转表示，slerp 插值、与矩阵互转
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 存储为 (w, x, y, z)，其中 w 为实部，(x,y,z) 为虚部（旋转轴×sin(θ/2))。
 */
#pragma once

#include "vec3.h"
#include "mat3.h"
#include "geo_math.h"

#include <cmath>

namespace mulan::geo {

template<typename T>
struct QuatT {
    T w{T(1)}, x{T(0)}, y{T(0)}, z{T(0)};   // 单位四元数默认

    constexpr QuatT() = default;
    constexpr QuatT(T w_, T x_, T y_, T z_) : w(w_), x(x_), y(y_), z(z_) {}

    // ---------- 工厂 ----------

    /// 单位四元数
    static constexpr QuatT identity() { return QuatT(T(1), T(0), T(0), T(0)); }

    /// 从单位轴 + 弧度构造
    static QuatT fromAxisAngle(const Vec3T<T>& axis, T rad) {
        T half = rad * T(0.5);
        T s = std::sin(half);
        Vec3T<T> n = axis.normalized();
        return QuatT(std::cos(half), n.x * s, n.y * s, n.z * s);
    }

    /// 从两个单位向量构造（将 from 旋转到 to）
    static QuatT fromTwoVectors(Vec3T<T> from, Vec3T<T> to) {
        from = from.normalized();
        to   = to.normalized();
        T d = dot(from, to);
        if (d >= T(1)) return identity();             // 平行
        if (d <= T(-1)) {                              // 反向，任选正交轴
            Vec3T<T> axis = std::abs(from.x) < T(0.9)
                ? cross(Vec3T<T>::unitX(), from)
                : cross(Vec3T<T>::unitY(), from);
            return fromAxisAngle(axis.normalized(), kPi);
        }
        Vec3T<T> axis = cross(from, to);
        T s = std::sqrt((T(1) + d) * T(2));
        T inv = T(1) / s;
        return QuatT(s * T(0.5), axis.x * inv, axis.y * inv, axis.z * inv).normalized();
    }

    // ---------- 算术 ----------

    /// 格拉斯曼积（q1 * q2）
    QuatT operator*(const QuatT& q) const {
        return QuatT(
            w*q.w - x*q.x - y*q.y - z*q.z,
            w*q.x + x*q.w + y*q.z - z*q.y,
            w*q.y - x*q.z + y*q.w + z*q.x,
            w*q.z + x*q.y - y*q.x + z*q.w);
    }

    QuatT conjugate() const { return QuatT(w, -x, -y, -z); }
    QuatT inverse() const {
        T n2 = normSq();
        if (n2 == T(0)) return identity();
        T inv = T(1) / n2;
        return QuatT(w * inv, -x * inv, -y * inv, -z * inv);
    }

    T normSq() const { return w*w + x*x + y*y + z*z; }
    T norm()   const { return std::sqrt(normSq()); }

    QuatT normalized() const {
        T n = norm();
        return (n > T(0)) ? QuatT(w/n, x/n, y/n, z/n) : identity();
    }

    // ---------- 旋转操作 ----------

    /// 旋转一个点/向量
    Vec3T<T> operator*(const Vec3T<T>& v) const {
        // v' = q * (0,v) * q^-1，优化版
        Vec3T<T> qv(x, y, z);
        Vec3T<T> t = cross(qv, v) * (T(2) * w);
        return v + cross(qv, t) + t * T(2);
    }

    /// 提取旋转轴与角度
    void toAxisAngle(Vec3T<T>& axis, T& angle) const {
        QuatT n = normalized();
        angle = T(2) * std::acos(clamp(n.w, T(-1), T(1)));
        T s = std::sqrt(T(1) - n.w * n.w);
        if (s < T(1e-9)) {
            axis = Vec3T<T>::unitX();   // 角度≈0，轴无意义
        } else {
            axis = Vec3T<T>(n.x, n.y, n.z) / s;
        }
    }

    // ---------- 矩阵互转 ----------

    Mat3T<T> toMat3() const {
        T xx = x*x, yy = y*y, zz = z*z;
        T xy = x*y, xz = x*z, yz = y*z;
        T wx = w*x, wy = w*y, wz = w*z;
        return Mat3T<T>(
            Vec3T<T>(T(1)-T(2)*(yy+zz), T(2)*(xy+wz),       T(2)*(xz-wy)),
            Vec3T<T>(T(2)*(xy-wz),     T(1)-T(2)*(xx+zz),  T(2)*(yz+wx)),
            Vec3T<T>(T(2)*(xz+wy),     T(2)*(yz-wx),       T(1)-T(2)*(xx+yy)));
    }
    Mat4T<T> toMat4() const {
        Mat3T<T> r = toMat3();
        return Mat4T<T>(
            Vec4T<T>(r[0].x, r[0].y, r[0].z, T(0)),
            Vec4T<T>(r[1].x, r[1].y, r[1].z, T(0)),
            Vec4T<T>(r[2].x, r[2].y, r[2].z, T(0)),
            Vec4T<T>(T(0),   T(0),   T(0),   T(1)));
    }

    static QuatT fromMat3(const Mat3T<T>& m) {
        T trace = m[0].x + m[1].y + m[2].z;
        if (trace > T(0)) {
            T s = std::sqrt(trace + T(1)) * T(2);   // s = 4*w
            return QuatT(T(0.25) * s,
                         (m[1].z - m[2].y) / s,
                         (m[2].x - m[0].z) / s,
                         (m[0].y - m[1].x) / s).normalized();
        } else if (m[0].x > m[1].y && m[0].x > m[2].z) {
            T s = std::sqrt(T(1) + m[0].x - m[1].y - m[2].z) * T(2); // s = 4*x
            return QuatT((m[1].z - m[2].y) / s,
                         T(0.25) * s,
                         (m[1].x + m[0].y) / s,
                         (m[2].x + m[0].z) / s).normalized();
        } else if (m[1].y > m[2].z) {
            T s = std::sqrt(T(1) + m[1].y - m[0].x - m[2].z) * T(2); // s = 4*y
            return QuatT((m[2].x - m[0].z) / s,
                         (m[1].x + m[0].y) / s,
                         T(0.25) * s,
                         (m[2].y + m[1].z) / s).normalized();
        } else {
            T s = std::sqrt(T(1) + m[2].z - m[0].x - m[1].y) * T(2); // s = 4*z
            return QuatT((m[0].y - m[1].x) / s,
                         (m[2].x + m[0].z) / s,
                         (m[2].y + m[1].z) / s,
                         T(0.25) * s).normalized();
        }
    }
};

// ---------- slerp（球面线性插值）----------

template<typename T>
QuatT<T> slerp(const QuatT<T>& a, QuatT<T> b, T t) {
    T cosTheta = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;
    if (cosTheta < T(0)) {   // 取短弧
        b = QuatT<T>(-b.w, -b.x, -b.y, -b.z);
        cosTheta = -cosTheta;
    }
    if (cosTheta > T(0.9995)) {
        // 接近共线，线性插值并归一化即可
        return QuatT<T>(a.w + (b.w-a.w)*t,
                        a.x + (b.x-a.x)*t,
                        a.y + (b.y-a.y)*t,
                        a.z + (b.z-a.z)*t).normalized();
    }
    T theta = std::acos(clamp(cosTheta, T(-1), T(1)));
    T sinTheta = std::sin(theta);
    T k0 = std::sin((T(1)-t)*theta) / sinTheta;
    T k1 = std::sin(t*theta) / sinTheta;
    return QuatT<T>(a.w*k0 + b.w*k1,
                    a.x*k0 + b.x*k1,
                    a.y*k0 + b.y*k1,
                    a.z*k0 + b.z*k1);
}

// ---------- 别名 ----------
using Quat  = QuatT<double>;
using FQuat = QuatT<float>;

/// 轴角构造四元数（glm angleAxis 等价，参数顺序：angle, axis）。
/// 替换 glm::angleAxis 时直接用 geo::angleAxis。
template<typename T>
inline QuatT<T> angleAxis(T angle, const Vec3T<T>& axis) {
    return QuatT<T>::fromAxisAngle(axis, angle);
}

} // namespace mulan::geo
