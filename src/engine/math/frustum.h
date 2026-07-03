/**
 * @file frustum.h
 * @brief 视锥体裁剪平面，用于可见性剔除
 * @author hxxcxx
 * @date 2026-04-20
 *
 * 与 AABB 同层级几何值类型，纯头文件 inline 实现。
 */

#pragma once

#include "math.h"
#include "aabb.h"

#include <cmath>

namespace mulan::engine {

struct Plane {
    Vec3  normal = {0, 1, 0};
    double distance = 0;   // n·x + d = 0

    double signedDistance(const Vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct Frustum {
    Plane planes[6]; // Left, Right, Bottom, Top, Near, Far

    /// Gribb-Hartmann 方法：从 view-projection 矩阵提取 6 裁剪平面
    static Frustum fromViewProjection(const Mat4& vp) {
        Frustum f;

        auto row = [&](int r) -> Vec4 {
            return Vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
        };

        auto extract = [](const Vec4& p) -> Plane {
            double len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 0) {
                return { Vec3{p.x / len, p.y / len, p.z / len}, p.w / len };
            }
            return { Vec3{0, 1, 0}, 0 };
        };

        f.planes[0] = extract(row(3) + row(0));
        f.planes[1] = extract(row(3) - row(0));
        f.planes[2] = extract(row(3) + row(1));
        f.planes[3] = extract(row(3) - row(1));
        f.planes[4] = extract(row(3) + row(2));
        f.planes[5] = extract(row(3) - row(2));

        return f;
    }

    bool contains(const Vec3& point) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].signedDistance(point) < 0)
                return false;
        }
        return true;
    }

    /// AABB 与视锥相交测试（p-vertex / n-vertex 优化）
    bool intersects(const AABB& box) const {
        for (int i = 0; i < 6; ++i) {
            Vec3 p = box.min;
            if (planes[i].normal.x >= 0) p.x = box.max.x;
            if (planes[i].normal.y >= 0) p.y = box.max.y;
            if (planes[i].normal.z >= 0) p.z = box.max.z;

            if (planes[i].signedDistance(p) < 0)
                return false;
        }
        return true;
    }
};

} // namespace mulan::engine
