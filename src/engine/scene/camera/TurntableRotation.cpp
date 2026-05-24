#include "TurntableRotation.h"

#include <algorithm>
#include <cmath>

namespace mulan::engine {

TurntableRotation::TurntableRotation()
    : m_yaw(kPi * 0.25)
    , m_pitch(kPi * 0.33)
{}

Vec3 TurntableRotation::forward() const {
    double cp = std::cos(m_pitch);
    return Vec3{cp * std::cos(m_yaw), cp * std::sin(m_yaw), std::sin(m_pitch)};
}

Vec3 TurntableRotation::right() const {
    Vec3 fwd = forward();
    return glm::normalize(glm::cross(fwd, Vec3{0, 0, 1}));
}

Vec3 TurntableRotation::up() const {
    Vec3 fwd = forward();
    Vec3 r   = glm::normalize(glm::cross(fwd, Vec3{0, 0, 1}));
    return glm::normalize(glm::cross(r, fwd));
}

void TurntableRotation::orbitDelta(double dx, double dy) {
    m_yaw   -= dx * m_orbitSpeed;
    m_pitch -= dy * m_orbitSpeed;
    m_pitch  = std::clamp(m_pitch, kMinPitch, kMaxPitch);
}

void TurntableRotation::setYawPitch(double yaw, double pitch) {
    m_yaw   = yaw;
    m_pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
}

void TurntableRotation::beginOrbit(int x, int y, int, int) {
    m_orbitPrevX = x;
    m_orbitPrevY = y;
    m_orbitDrag  = true;
}

void TurntableRotation::orbitToPoint(int x, int y, int, int) {
    if (!m_orbitDrag) return;
    int dx = x - m_orbitPrevX;
    int dy = y - m_orbitPrevY;
    m_orbitPrevX = x;
    m_orbitPrevY = y;
    if (dx == 0 && dy == 0) return;
    m_yaw   -= static_cast<double>(dx) * m_orbitSpeed;
    m_pitch -= static_cast<double>(dy) * m_orbitSpeed;
    m_pitch  = std::clamp(m_pitch, kMinPitch, kMaxPitch);
}

void TurntableRotation::endOrbit() {
    m_orbitDrag = false;
}

} // namespace mulan::Engine
