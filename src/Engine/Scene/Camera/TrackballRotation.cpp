#include "TrackballRotation.h"

#include <algorithm>
#include <cmath>

namespace MulanGeo::engine {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TrackballRotation::TrackballRotation() {
    // 默认朝向与 Turntable 一致：yaw=45°, pitch=59.4°
    // 使得两种模式的初始视角相同
    Quat qYaw   = glm::angleAxis(-kPi * 0.25, Vec3{0, 0, 1});
    Quat qPitch = glm::angleAxis(kPi * 0.33,  Vec3{1, 0, 0});
    m_rotation = glm::normalize(qYaw * qPitch);
}

Vec3 TrackballRotation::forward() const {
    Vec3 fwd = m_rotation * Vec3{0, 1, 0};
    return glm::normalize(fwd);
}

Vec3 TrackballRotation::right() const {
    Vec3 r = m_rotation * Vec3{1, 0, 0};
    return glm::normalize(r);
}

Vec3 TrackballRotation::up() const {
    Vec3 u = m_rotation * Vec3{0, 0, 1};
    return glm::normalize(u);
}

void TrackballRotation::orbitDelta(double dx, double dy) {
    double angle = std::sqrt(dx * dx + dy * dy) * 0.005;
    if (angle < 1e-10) return;

    Vec3 axis = right() * dy - up() * dx;
    double len = glm::length(axis);
    if (len < 1e-10) return;
    axis = axis / len;

    Quat deltaQ = glm::angleAxis(angle, axis);
    m_rotation = glm::normalize(deltaQ * m_rotation);
}

// ============================================================
// Arcball 交互
// ============================================================

Vec3 TrackballRotation::arcballProject(int x, int y, int viewW, int viewH) const {
    double nx = (2.0 * x - viewW)  / viewW;
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
    return Vec3{nx, ny, nz};
}

void TrackballRotation::beginOrbit(int x, int y, int viewW, int viewH) {
    m_arcballPrev = arcballProject(x, y, viewW, viewH);
    m_arcballActive = true;
}

void TrackballRotation::orbitToPoint(int x, int y, int viewW, int viewH) {
    if (!m_arcballActive) return;

    Vec3 curr = arcballProject(x, y, viewW, viewH);

    Vec3 axis = glm::cross(curr, m_arcballPrev);
    double axisLen = glm::length(axis);

    if (axisLen < 1e-10) return;

    double dot = glm::dot(curr, m_arcballPrev);
    double angle = std::atan2(axisLen, dot) * m_arcballSpeed;

    Vec3 ndcAxis = axis / axisLen;
    Vec3 worldAxis = right() * ndcAxis.x + up() * ndcAxis.y + forward() * ndcAxis.z;
    worldAxis = glm::normalize(worldAxis);

    Quat deltaQ = glm::angleAxis(angle, worldAxis);
    m_rotation = glm::normalize(deltaQ * m_rotation);

    m_arcballPrev = curr;
}

void TrackballRotation::endOrbit() {
    m_arcballActive = false;
}

} // namespace MulanGeo::Engine
