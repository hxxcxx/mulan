#include "work_plane.h"

#include <mulan/math/algo/intersect.h>

namespace mulan::engine {

WorkPlane WorkPlane::worldXY() {
    return WorkPlane(math::Plane3::fromPointNormal(math::Point3::origin(), math::Vec3(0.0, 0.0, 1.0)));
}

WorkPlane WorkPlane::worldXZ() {
    return WorkPlane(math::Plane3::fromPointNormal(math::Point3::origin(), math::Vec3(0.0, 1.0, 0.0)));
}

WorkPlane WorkPlane::worldYZ() {
    return WorkPlane(math::Plane3::fromPointNormal(math::Point3::origin(), math::Vec3(1.0, 0.0, 0.0)));
}

WorkPlane WorkPlane::fromView(const Camera& camera) {
    return WorkPlane(math::Plane3::fromPointNormal(math::Point3(camera.target()), -camera.forward()));
}

std::optional<math::Point3> WorkPlane::intersectScreen(const Camera& camera, double screenX, double screenY) const {
    const auto hit = math::intersect(camera.screenRay(screenX, screenY), plane_);
    if (!hit.hit) {
        return std::nullopt;
    }
    return hit.point;
}

}  // namespace mulan::engine
