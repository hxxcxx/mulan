/**
 * @file hit.h
 * @brief 求交结果结构 — 命中标记 + 交点 + 参数 t
 * @author hxxcxx
 * @date 2026-06-29
 *
 * 统一的求交返回值：
 *  - hit    是否命中
 *  - point  交点坐标（命中时有效）
 *  - t      参数（射线/线段上的参数，光线 origin+dir*t；线段 start+dir*t∈[0,1]）
 */
#pragma once

#include "linalg/vec3.h"

namespace mulan::math {

/// 3D 求交结果
struct Hit3 {
    bool   hit   = false;
    Vec3   point {};
    double t     = 0.0;

    static Hit3 miss() { return Hit3{}; }
    static Hit3 make(const Vec3& p, double t_) { return Hit3{true, p, t_}; }
};

/// 2D 求交结果
struct Hit2 {
    bool   hit   = false;
    Vec2   point {};
    double t     = 0.0;

    static Hit2 miss() { return Hit2{}; }
    static Hit2 make(const Vec2& p, double t_) { return Hit2{true, p, t_}; }
};

} // namespace mulan::math
