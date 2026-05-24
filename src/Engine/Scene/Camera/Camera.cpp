#include "Camera.h"
#include "TurntableRotation.h"
#include "TrackballRotation.h"

#include <algorithm>
#include <cmath>

namespace MulanGeo::engine {

// ============================================================
// 构造 / 模式切换
// ============================================================

void Camera::createRotation(CameraMode mode) {
    if (mode == CameraMode::Turntable) {
        m_active = std::make_unique<TurntableRotation>();
    } else {
        m_active = std::make_unique<TrackballRotation>();
    }
}

Camera::Camera(CameraMode initialMode)
    : m_mode(initialMode)
{
    createRotation(initialMode);
}

void Camera::setMode(CameraMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    createRotation(mode);
}

// ============================================================
// 模式专用访问
// ============================================================

double Camera::yaw() const { return m_active->yaw(); }
double Camera::pitch() const { return m_active->pitch(); }
void Camera::setYawPitch(double yaw, double pitch) { m_active->setYawPitch(yaw, pitch); }

Quat Camera::rotation() const { return m_active->rotation(); }
void Camera::setRotation(const Quat& q) { m_active->setRotation(q); }

// ============================================================
// 交互
// ============================================================

void Camera::orbitDelta(double dx, double dy) {
    m_active->orbitDelta(dx, dy);
}

void Camera::beginOrbit(int x, int y) {
    m_active->beginOrbit(x, y, m_width, m_height);
}

void Camera::orbitToPoint(int x, int y) {
    m_active->orbitToPoint(x, y, m_width, m_height);
}

void Camera::endOrbit() {
    m_active->endOrbit();
}

void Camera::pan(double dx, double dy) {
    Vec3 r = right();
    Vec3 u = up();
    double scale = (m_ortho ? m_orthoSize : m_distance) * m_panSpeed;
    m_target = m_target - r * (dx * scale) + u * (dy * scale);
}

void Camera::zoom(double delta) {
    if (m_ortho) {
        double factor = std::pow(m_zoomSpeed, delta);
        m_orthoSize = std::max(m_orthoSize * factor, m_minDistance);
    } else {
        double factor = std::pow(m_zoomSpeed, delta);
        m_distance = std::max(m_distance * factor, m_minDistance);
    }
}

void Camera::fitToBox(const AABB& box, double padding) {
    m_target = box.center();
    double radius = glm::length(box.max - box.min) * 0.5;

    if (m_ortho) {
        m_orthoSize = radius * padding;
        m_distance  = radius * padding * 2.0;
    } else {
        double halfFov = m_fovY * 0.5;
        m_distance = radius * padding / std::sin(halfFov);
    }

    if (m_distance < m_minDistance) m_distance = radius * 5.0 + 1.0;

    m_nearZ = m_distance * 0.01;
    m_farZ  = m_distance * 10.0;
}

// ============================================================
// 速度参数
// ============================================================

void Camera::setOrbitSpeed(double s) {
    m_active->setOrbitSpeed(s);
}

double Camera::orbitSpeed() const {
    return m_active->orbitSpeed();
}

// ============================================================
// 矩阵计算
// ============================================================

Vec3 Camera::eyePosition() const {
    return m_target - m_active->forward() * m_distance;
}

Mat4 Camera::viewMatrix() const {
    Vec3 r   = m_active->right();
    Vec3 u   = m_active->up();
    Vec3 fwd = m_active->forward();
    Vec3 eye = m_target - fwd * m_distance;

    Mat4 v(1.0);
    v[0][0] = r.x;    v[1][0] = r.y;    v[2][0] = r.z;    v[3][0] = -glm::dot(r, eye);
    v[0][1] = u.x;    v[1][1] = u.y;    v[2][1] = u.z;    v[3][1] = -glm::dot(u, eye);
    v[0][2] = -fwd.x; v[1][2] = -fwd.y; v[2][2] = -fwd.z; v[3][2] = glm::dot(fwd, eye);
    v[0][3] = 0;      v[1][3] = 0;      v[2][3] = 0;      v[3][3] = 1;
    return v;
}

Mat4 Camera::projectionMatrix() const {
    if (m_ortho) {
        double h = m_orthoSize;
        double w = h * aspect();
        return glm::ortho(-w, w, -h, h, m_nearZ, m_farZ);
    }
    return glm::perspective(m_fovY, aspect(), m_nearZ, m_farZ);
}

Mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

Mat4 Camera::rotationMatrix() const {
    Vec3 r   = m_active->right();
    Vec3 u   = m_active->up();
    Vec3 fwd = m_active->forward();

    Mat4 rm(1.0);
    rm[0][0] = r.x;    rm[1][0] = r.y;    rm[2][0] = r.z;    rm[3][0] = 0;
    rm[0][1] = u.x;    rm[1][1] = u.y;    rm[2][1] = u.z;    rm[3][1] = 0;
    rm[0][2] = -fwd.x; rm[1][2] = -fwd.y; rm[2][2] = -fwd.z; rm[3][2] = 0;
    rm[0][3] = 0;      rm[1][3] = 0;      rm[2][3] = 0;      rm[3][3] = 1;
    return rm;
}

Frustum Camera::frustum() const {
    return Frustum::fromViewProjection(viewProjectionMatrix());
}

Vec3 Camera::forward() const { return m_active->forward(); }
Vec3 Camera::right()   const { return m_active->right(); }
Vec3 Camera::up()      const { return m_active->up(); }

} // namespace MulanGeo::Engine
