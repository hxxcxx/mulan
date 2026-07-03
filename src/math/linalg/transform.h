/**
 * @file transform.h
 * @brief 组合变换（2D / 3D）— 平移 + 旋转 + 缩放
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 变换顺序（先缩放，再旋转，最后平移）：
 *   M = Translate * Rotate * Scale
 * 便于场景图的父子层级累积。
 */
#pragma once

#include "vec2.h"
#include "vec3.h"
#include "mat2.h"
#include "mat3.h"
#include "mat4.h"
#include "quaternion.h"

namespace mulan::geo {

// ============================================================
// Transform3 — 3D 组合变换
// ============================================================

struct Transform3 {
    Vec3  translation{0.0, 0.0, 0.0};
    Quat  rotation = Quat::identity();
    Vec3  scale    {1.0, 1.0, 1.0};

    constexpr Transform3() = default;
    constexpr Transform3(const Vec3& t, const Quat& r, const Vec3& s)
        : translation(t), rotation(r), scale(s) {}

    /// M = Translate * Rotate * Scale
    Mat4 toMatrix() const {
        Mat4 m = Mat4::translate(translation)
               * rotation.toMat4()
               * Mat4::scale(scale);
        return m;
    }

    /// 逆变换矩阵
    Mat4 toInverseMatrix() const {
        Vec3 invScale(1.0 / scale.x, 1.0 / scale.y, 1.0 / scale.z);
        return Mat4::scale(invScale)
             * rotation.conjugate().toMat4()
             * Mat4::translate(-translation);
    }

    /// 父变换 * 子变换（场景图累积）
    Transform3 combine(const Transform3& child) const {
        Transform3 r;
        r.scale = Vec3(scale.x * child.scale.x,
                       scale.y * child.scale.y,
                       scale.z * child.scale.z);
        r.rotation = rotation * child.rotation;
        r.translation = translation + rotation * (scale * child.translation);
        return r;
    }
};

// ============================================================
// Transform2 — 2D 组合变换
// ============================================================

struct Transform2 {
    Vec2   translation{0.0, 0.0};
    double rotation = 0.0;   // 弧度
    Vec2   scale    {1.0, 1.0};

    constexpr Transform2() = default;
    constexpr Transform2(const Vec2& t, double r, const Vec2& s)
        : translation(t), rotation(r), scale(s) {}

    Mat3 toMatrix() const {
        // 2D 用 3x3 齐次矩阵：Translate * Rotate * Scale
        Mat3 m = Mat3::scale(scale.x, scale.y, 1.0);
        m = Mat3::rotation(Vec3::unitZ(), rotation) * m;
        // 平移写入第 3 列
        m[2] = Vec3(translation.x, translation.y, 1.0);
        return m;
    }
};

} // namespace mulan::geo
