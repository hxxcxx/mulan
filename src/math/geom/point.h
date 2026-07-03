/**
 * @file point.h
 * @brief 点（2D / 3D）— 与向量语义区分的独立类型
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 设计理由：
 *  - 点（位置）与向量（位移）数学上同构但语义不同。
 *  - 用独立类型 Point 与 Vec 区分，可在编译期挡住无意义运算，
 *    例如 Point + Point（两个位置相加无几何意义）。
 *
 * 允许的运算（仿射几何语义）：
 *   Point - Point = Vec      （两点差为位移向量）
 *   Point + Vec   = Point    （点沿向量平移）
 *   Point - Vec   = Point
 *   Point += Vec / -= Vec
 *   -Point                   （取关于原点的对称点，等价 0 - Point，提供以方便）
 *   Point == Point
 *
 * 故意不提供：
 *   Point + Point            （无意义）
 *   Point * scalar           （缩放点应显式走 Transform/原点）
 *
 * 与 Vec 的互转：显式构造/转换，避免隐式穿透破坏类型边界。
 */
#pragma once

#include "../linalg/vec2.h"
#include "../linalg/vec3.h"
#include "../linalg/mat4.h"
#include "../scalar/tolerance.h"

#include <cmath>

namespace mulan::math {

// 前向声明（Mat4T 已由 Mat4.h 提供，此处不重复）

// ============================================================
// Point3
// ============================================================

struct Point3 {
    double x{};
    double y{};
    double z{};

    constexpr Point3() = default;
    constexpr Point3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    // ---------- 显式与 Vec 互转 ----------

    explicit constexpr Point3(const Vec3& v) : x(v.x), y(v.y), z(v.z) {}
    constexpr Vec3 asVec() const { return Vec3(x, y, z); }
    explicit operator Vec3() const { return asVec(); }

    // ---------- 仿射运算 ----------

    Point3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Point3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }

    // 下标（方便与 Vec 风格代码互通）
    double&       operator[](int i)       { return (i == 0) ? x : (i == 1) ? y : z; }
    const double& operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : z; }

    // ---------- 工厂 ----------
    static constexpr Point3 origin() { return Point3(0.0, 0.0, 0.0); }

    // ---------- 查询 ----------
    bool isEqual(const Point3& o, const Tolerance& tol = defaultTolerance()) const {
        return distance(o) <= tol.lengthEps;
    }
    double distance(const Point3& o) const { return (asVec() - o.asVec()).length(); }
    double distanceSq(const Point3& o) const { return (asVec() - o.asVec()).lengthSq(); }

    // ---------- 矩阵变换（成员声明；定义见文件末尾）----------
    template<typename U> Point3 transformedBy(const Mat4T<U>& m) const;
};

// ---------- 仿射自由函数 ----------

/// Point - Point = Vec
inline constexpr Vec3 operator-(const Point3& a, const Point3& b) {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
/// Point + Vec = Point
inline constexpr Point3 operator+(const Point3& p, const Vec3& v) {
    return Point3(p.x + v.x, p.y + v.y, p.z + v.z);
}
inline constexpr Point3 operator+(const Vec3& v, const Point3& p) { return p + v; }
/// Point - Vec = Point
inline constexpr Point3 operator-(const Point3& p, const Vec3& v) {
    return Point3(p.x - v.x, p.y - v.y, p.z - v.z);
}

/// 取关于原点的对称点
inline constexpr Point3 operator-(const Point3& p) {
    return Point3(-p.x, -p.y, -p.z);
}

inline bool operator==(const Point3& a, const Point3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline bool operator!=(const Point3& a, const Point3& b) { return !(a == b); }

/// 线性插值两点（t∈[0,1]），结果为 Point
inline Point3 lerp(const Point3& a, const Point3& b, double t) {
    return Point3(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t);
}

/// 两点距离（便捷）
inline double distance(const Point3& a, const Point3& b) {
    return a.distance(b);
}

// ============================================================
// Point2
// ============================================================

struct Point2 {
    double x{};
    double y{};

    constexpr Point2() = default;
    constexpr Point2(double x_, double y_) : x(x_), y(y_) {}

    explicit constexpr Point2(const Vec2& v) : x(v.x), y(v.y) {}
    constexpr Vec2 asVec() const { return Vec2(x, y); }
    explicit operator Vec2() const { return asVec(); }

    Point2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Point2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }

    double&       operator[](int i)       { return (i == 0) ? x : y; }
    const double& operator[](int i) const { return (i == 0) ? x : y; }

    static constexpr Point2 origin() { return Point2(0.0, 0.0); }

    bool isEqual(const Point2& o, const Tolerance& tol = defaultTolerance()) const {
        return distance(o) <= tol.lengthEps;
    }
    double distance(const Point2& o) const { return (asVec() - o.asVec()).length(); }
    double distanceSq(const Point2& o) const { return (asVec() - o.asVec()).lengthSq(); }
};

inline constexpr Vec2 operator-(const Point2& a, const Point2& b) {
    return Vec2(a.x - b.x, a.y - b.y);
}
inline constexpr Point2 operator+(const Point2& p, const Vec2& v) {
    return Point2(p.x + v.x, p.y + v.y);
}
inline constexpr Point2 operator+(const Vec2& v, const Point2& p) { return p + v; }
inline constexpr Point2 operator-(const Point2& p, const Vec2& v) {
    return Point2(p.x - v.x, p.y - v.y);
}
inline constexpr Point2 operator-(const Point2& p) {
    return Point2(-p.x, -p.y);
}

inline bool operator==(const Point2& a, const Point2& b) {
    return a.x == b.x && a.y == b.y;
}
inline bool operator!=(const Point2& a, const Point2& b) { return !(a == b); }

inline Point2 lerp(const Point2& a, const Point2& b, double t) {
    return Point2(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t);
}

inline double distance(const Point2& a, const Point2& b) {
    return a.distance(b);
}

// ============================================================
// 矩阵变换成员定义（类内声明，此处定义；Point.h 已 include Mat4.h）
// ============================================================

// ---- Vec3 的方向 / 法向变换 ----

template<typename T>
template<typename U>
inline Vec3T<T> Vec3T<T>::transformedAsDir(const Mat4T<U>& m) const {
    // w = 0，忽略平移：取左上 3x3
    return Vec3T<T>(static_cast<T>(m[0].x * x + m[1].x * y + m[2].x * z),
                    static_cast<T>(m[0].y * x + m[1].y * y + m[2].y * z),
                    static_cast<T>(m[0].z * x + m[1].z * y + m[2].z * z));
}

template<typename T>
template<typename U>
inline Vec3T<T> Vec3T<T>::transformedAsNormal(const Mat4T<U>& m) const {
    // 法向变换 = (M^-1)^T 的左上 3x3 作用于向量
    Mat4T<U> invT = m.inverse().transposed();
    Vec3T<T> n = Vec3T<T>(static_cast<T>(invT[0].x * x + invT[1].x * y + invT[2].x * z),
                          static_cast<T>(invT[0].y * x + invT[1].y * y + invT[2].y * z),
                          static_cast<T>(invT[0].z * x + invT[1].z * y + invT[2].z * z));
    return n.normalized();
}

// ---- Point3 的点变换（w = 1，含平移）----

template<typename U>
inline Point3 Point3::transformedBy(const Mat4T<U>& m) const {
    Vec4 r = m * Vec4(x, y, z, 1.0);
    return Point3(r.x, r.y, r.z);
}

} // namespace mulan::math
