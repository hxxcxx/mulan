/**
 * @file work_plane.h
 * @brief WorkPlane 定义绘制工具使用的世界空间工作平面
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 绘制工具从屏幕坐标得到的是一条世界空间射线。WorkPlane 负责把射线落到一个确定
 * 平面上，从而得到可写入草图的三维点。第一阶段默认使用世界 XY 平面。
 */
#pragma once

#include "../render/camera/camera.h"

#include <mulan/math/math.h>

#include <optional>

namespace mulan::engine {

class WorkPlane {
public:
    WorkPlane() = default;
    explicit WorkPlane(const math::Plane3& plane) : plane_(plane) {}

    static WorkPlane worldXY();
    static WorkPlane worldXZ();
    static WorkPlane worldYZ();
    static WorkPlane fromView(const Camera& camera);

    const math::Plane3& plane() const { return plane_; }

    std::optional<math::Point3> intersectScreen(const Camera& camera, double screenX, double screenY) const;

private:
    math::Plane3 plane_ = math::Plane3::fromPointNormal(math::Point3::origin(), math::Vec3(0.0, 0.0, 1.0));
};

}  // namespace mulan::engine
