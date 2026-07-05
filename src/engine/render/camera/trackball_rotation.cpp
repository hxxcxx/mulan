#include "trackball_rotation.h"

#include <algorithm>
#include <cmath>

namespace mulan::engine {

TrackballRotation::TrackballRotation() {
    // 默认朝向与 Turntable 一致：yaw=45°, pitch=59.4°
    // 使得两种模式的初始视角相同
    math::Quat qYaw = math::Quat::fromAxisAngle(math::Vec3{ 0, 0, 1 }, -math::kPi * 0.25);
    math::Quat qPitch = math::Quat::fromAxisAngle(math::Vec3{ 1, 0, 0 }, math::kPi * 0.33);
    rotation_ = (qYaw * qPitch).normalized();
}

math::Vec3 TrackballRotation::forward() const {
    math::Vec3 fwd = rotation_ * math::Vec3{ 0, 1, 0 };
    return fwd.normalized();
}

math::Vec3 TrackballRotation::right() const {
    math::Vec3 r = rotation_ * math::Vec3{ 1, 0, 0 };
    return r.normalized();
}

math::Vec3 TrackballRotation::up() const {
    math::Vec3 u = rotation_ * math::Vec3{ 0, 0, 1 };
    return u.normalized();
}

void TrackballRotation::orbitDelta(double dx, double dy) {
    double angle = std::sqrt(dx * dx + dy * dy) * 0.005;
    if (angle < 1e-10)
        return;

    math::Vec3 axis = right() * dy - up() * dx;
    double len = axis.length();
    if (len < 1e-10)
        return;
    axis = axis / len;

    math::Quat deltaQ = math::Quat::fromAxisAngle(axis, angle);
    rotation_ = (deltaQ * rotation_).normalized();
}

// ============================================================
// Arcball 交互
// ============================================================

math::Vec3 TrackballRotation::arcballProject(int x, int y, int viewW, int viewH) const {
    double nx = (2.0 * x - viewW) / viewW;
    double ny = (viewH - 2.0 * y) / viewH;

    double len2 = nx * nx + ny * ny;
    double nz;
    if (len2 <= 1.0) {
        nz = std::sqrt(1.0 - len2);
    } else {
        double len = std::sqrt(len2);
        nx /= len;
        ny /= len;
        nz = 0.0;
    }
    return math::Vec3{ nx, ny, nz };
}

void TrackballRotation::beginOrbit(int x, int y, int viewW, int viewH) {
    arcball_prev_ = arcballProject(x, y, viewW, viewH);
    arcball_active_ = true;
}

void TrackballRotation::orbitToPoint(int x, int y, int viewW, int viewH) {
    if (!arcball_active_)
        return;

    math::Vec3 curr = arcballProject(x, y, viewW, viewH);

    math::Vec3 axis = curr.cross(arcball_prev_);
    double axisLen = axis.length();

    if (axisLen < 1e-10)
        return;

    double dotValue = curr.dot(arcball_prev_);
    double angle = std::atan2(axisLen, dotValue) * arcball_speed_;

    math::Vec3 ndcAxis = axis / axisLen;
    math::Vec3 worldAxis = right() * ndcAxis.x + up() * ndcAxis.y + forward() * ndcAxis.z;
    worldAxis = worldAxis.normalized();

    math::Quat deltaQ = math::Quat::fromAxisAngle(worldAxis, angle);
    rotation_ = (deltaQ * rotation_).normalized();

    arcball_prev_ = curr;
}

void TrackballRotation::endOrbit() {
    arcball_active_ = false;
}

}  // namespace mulan::engine
