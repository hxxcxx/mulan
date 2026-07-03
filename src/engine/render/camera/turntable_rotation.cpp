#include "turntable_rotation.h"

#include <algorithm>
#include <cmath>

namespace mulan::engine {

TurntableRotation::TurntableRotation()
    : yaw_(kPi * 0.25)
    , pitch_(kPi * 0.33)
{}

Vec3 TurntableRotation::forward() const {
    double cp = std::cos(pitch_);
    return Vec3{cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_)};
}

Vec3 TurntableRotation::right() const {
    Vec3 fwd = forward();
    return normalize(cross(fwd, Vec3{0, 0, 1}));
}

Vec3 TurntableRotation::up() const {
    Vec3 fwd = forward();
    Vec3 r   = normalize(cross(fwd, Vec3{0, 0, 1}));
    return normalize(cross(r, fwd));
}

void TurntableRotation::orbitDelta(double dx, double dy) {
    yaw_   -= dx * orbit_speed_;
    pitch_ -= dy * orbit_speed_;
    pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
}

void TurntableRotation::setYawPitch(double yaw, double pitch) {
    yaw_   = yaw;
    pitch_ = std::clamp(pitch, kMinPitch, kMaxPitch);
}

void TurntableRotation::beginOrbit(int x, int y, int, int) {
    orbit_prev_x_ = x;
    orbit_prev_y_ = y;
    orbit_drag_  = true;
}

void TurntableRotation::orbitToPoint(int x, int y, int, int) {
    if (!orbit_drag_) return;
    int dx = x - orbit_prev_x_;
    int dy = y - orbit_prev_y_;
    orbit_prev_x_ = x;
    orbit_prev_y_ = y;
    if (dx == 0 && dy == 0) return;
    yaw_   -= static_cast<double>(dx) * orbit_speed_;
    pitch_ -= static_cast<double>(dy) * orbit_speed_;
    pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
}

void TurntableRotation::endOrbit() {
    orbit_drag_ = false;
}

} // namespace mulan::engine
