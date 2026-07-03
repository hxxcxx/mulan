/**
 * @file frustum.h
 * @brief View frustum extraction and culling helpers.
 *
 * Frustum3 stores six inward-facing clip planes in the same convention as
 * Plane3: normal dot point = d, and inside points have signedDistance >= 0.
 */
#pragma once

#include "aabb.h"
#include "../linalg/mat4.h"
#include "plane.h"
#include "sphere.h"
#include "../linalg/vec4.h"

#include <cmath>

namespace mulan::math {

enum class FrustumPlane : int {
    Left = 0,
    Right,
    Bottom,
    Top,
    Near,
    Far,
    Count
};

struct Frustum3 {
    Plane3 planes[static_cast<int>(FrustumPlane::Count)]{};

    Plane3& operator[](FrustumPlane plane) {
        return planes[static_cast<int>(plane)];
    }

    const Plane3& operator[](FrustumPlane plane) const {
        return planes[static_cast<int>(plane)];
    }

    static Frustum3 fromViewProjection(const Mat4& vp) {
        Frustum3 f;

        auto row = [&](int r) -> Vec4 {
            return Vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
        };

        auto extract = [](const Vec4& p) -> Plane3 {
            double len = Vec3(p).length();
            if (len <= 0.0) {
                return Plane3(Vec3::unitY(), 0.0);
            }
            Vec3 normal(p.x / len, p.y / len, p.z / len);
            return Plane3(normal, -p.w / len);
        };

        f[FrustumPlane::Left]   = extract(row(3) + row(0));
        f[FrustumPlane::Right]  = extract(row(3) - row(0));
        f[FrustumPlane::Bottom] = extract(row(3) + row(1));
        f[FrustumPlane::Top]    = extract(row(3) - row(1));
        f[FrustumPlane::Near]   = extract(row(3) + row(2));
        f[FrustumPlane::Far]    = extract(row(3) - row(2));

        return f;
    }

    bool contains(const Vec3& point, const Tolerance& tol = defaultTolerance()) const {
        for (const Plane3& plane : planes) {
            if (plane.signedDistance(point) < -tol.lengthEps) {
                return false;
            }
        }
        return true;
    }

    bool intersects(const AABB3& box, const Tolerance& tol = defaultTolerance()) const {
        if (box.isEmpty(tol)) return false;

        for (const Plane3& plane : planes) {
            Vec3 p = box.min;
            if (plane.normal.x >= 0.0) p.x = box.max.x;
            if (plane.normal.y >= 0.0) p.y = box.max.y;
            if (plane.normal.z >= 0.0) p.z = box.max.z;

            if (plane.signedDistance(p) < -tol.lengthEps) {
                return false;
            }
        }
        return true;
    }

    bool intersects(const Sphere3& sphere, const Tolerance& tol = defaultTolerance()) const {
        if (!sphere.isValid()) return false;

        for (const Plane3& plane : planes) {
            if (plane.signedDistance(sphere.center) < -sphere.radius - tol.lengthEps) {
                return false;
            }
        }
        return true;
    }
};

using Frustum = Frustum3;

} // namespace mulan::math
