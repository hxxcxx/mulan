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
    applyScreenOrbit(dx, dy, 800, 800);
}

// ============================================================
// 屏幕空间轨道旋转
// ============================================================

void TrackballRotation::applyScreenOrbit(double dx, double dy, int viewW, int viewH) {
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) {
        return;
    }

    const double viewportPixels = static_cast<double>(std::max(1, std::min(viewW, viewH)));
    const double radiansPerPixel = orbit_speed_ / viewportPixels;
    const double yawAngle = -dx * radiansPerPixel;
    const double pitchAngle = -dy * radiansPerPixel;

    const math::Quat yaw = math::Quat::fromAxisAngle(up(), yawAngle);
    const math::Quat pitch = math::Quat::fromAxisAngle(right(), pitchAngle);
    rotation_ = (pitch * yaw * rotation_).normalized();
}

void TrackballRotation::beginOrbit(int x, int y, int, int) {
    orbit_prev_x_ = x;
    orbit_prev_y_ = y;
    orbit_drag_ = true;
}

void TrackballRotation::orbitToPoint(int x, int y, int viewW, int viewH) {
    if (!orbit_drag_)
        return;

    const int dx = x - orbit_prev_x_;
    const int dy = y - orbit_prev_y_;
    orbit_prev_x_ = x;
    orbit_prev_y_ = y;
    applyScreenOrbit(static_cast<double>(dx), static_cast<double>(dy), viewW, viewH);
}

void TrackballRotation::endOrbit() {
    orbit_drag_ = false;
}

}  // namespace mulan::engine
