/**
 * @file Frustum.h
 * @brief 视锥体裁剪平面，用于可见性剔除
 * @author hxxcxx
 * @date 2026-04-20
 */

#pragma once

#include "../Math/Math.h"
#include "../Math/AABB.h"

namespace MulanGeo::engine {

struct Plane {
    Vec3  normal = {0, 1, 0};
    double distance = 0;   // n·x + d = 0

    double signedDistance(const Vec3& point) const;
};

struct Frustum {
    Plane planes[6]; // Left, Right, Bottom, Top, Near, Far

    static Frustum fromViewProjection(const Mat4& vp);

    bool contains(const Vec3& point) const;

    bool intersects(const AABB& box) const;
};

} // namespace MulanGeo::Engine
