#include "camera.h"
#include "turntable_rotation.h"
#include "trackball_rotation.h"

#include <algorithm>
#include <cmath>

namespace mulan::engine {

// ============================================================
// 构造 / 模式切换
// ============================================================

void Camera::createRotation(CameraMode mode) {
    if (mode == CameraMode::Turntable) {
        active_ = std::make_unique<TurntableRotation>();
    } else {
        active_ = std::make_unique<TrackballRotation>();
    }
}

Camera::Camera(CameraMode initialMode)
    : mode_(initialMode)
{
    createRotation(initialMode);
}

void Camera::setMode(CameraMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    createRotation(mode);
}

// ============================================================
// 模式专用访问
// ============================================================

double Camera::yaw() const { return active_->yaw(); }
double Camera::pitch() const { return active_->pitch(); }
void Camera::setYawPitch(double yaw, double pitch) { active_->setYawPitch(yaw, pitch); }

Quat Camera::rotation() const { return active_->rotation(); }
void Camera::setRotation(const Quat& q) { active_->setRotation(q); }

// ============================================================
// 交互
// ============================================================

void Camera::orbitDelta(double dx, double dy) {
    active_->orbitDelta(dx, dy);
}

void Camera::beginOrbit(int x, int y) {
    active_->beginOrbit(x, y, width_, height_);
}

void Camera::orbitToPoint(int x, int y) {
    active_->orbitToPoint(x, y, width_, height_);
}

void Camera::endOrbit() {
    active_->endOrbit();
}

void Camera::pan(double dx, double dy) {
    Vec3 r = right();
    Vec3 u = up();
    double scale = (ortho_ ? ortho_size_ : distance_) * pan_speed_;
    target_ = target_ - r * (dx * scale) + u * (dy * scale);
}

void Camera::zoom(double delta) {
    if (ortho_) {
        double factor = std::pow(zoom_speed_, delta);
        ortho_size_ = std::max(ortho_size_ * factor, min_distance_);
    } else {
        double factor = std::pow(zoom_speed_, delta);
        distance_ = std::max(distance_ * factor, min_distance_);
    }
}

void Camera::fitToBox(const AABB& box, double padding) {
    target_ = box.center();
    double radius = (box.max - box.min).length() * 0.5;

    if (ortho_) {
        ortho_size_ = radius * padding;
        distance_  = radius * padding * 2.0;
    } else {
        double halfFov = fov_y_ * 0.5;
        distance_ = radius * padding / std::sin(halfFov);
    }

    if (distance_ < min_distance_) distance_ = radius * 5.0 + 1.0;

    near_z_ = distance_ * 0.01;
    far_z_  = distance_ * 10.0;
}

// ============================================================
// 速度参数
// ============================================================

void Camera::setOrbitSpeed(double s) {
    active_->setOrbitSpeed(s);
}

double Camera::orbitSpeed() const {
    return active_->orbitSpeed();
}

// ============================================================
// 矩阵计算
// ============================================================

Vec3 Camera::eyePosition() const {
    return target_ - active_->forward() * distance_;
}

Mat4 Camera::viewMatrix() const {
    Vec3 r   = active_->right();
    Vec3 u   = active_->up();
    Vec3 fwd = active_->forward();
    Vec3 eye = target_ - fwd * distance_;

    Mat4 v(1.0);
    v[0][0] = r.x;    v[1][0] = r.y;    v[2][0] = r.z;    v[3][0] = -r.dot(eye);
    v[0][1] = u.x;    v[1][1] = u.y;    v[2][1] = u.z;    v[3][1] = -u.dot(eye);
    v[0][2] = -fwd.x; v[1][2] = -fwd.y; v[2][2] = -fwd.z; v[3][2] = fwd.dot(eye);
    v[0][3] = 0;      v[1][3] = 0;      v[2][3] = 0;      v[3][3] = 1;
    return v;
}

Mat4 Camera::projectionMatrix() const {
    if (ortho_) {
        double h = ortho_size_;
        double w = h * aspect();
        return ortho(-w, w, -h, h, near_z_, far_z_);
    }
    return perspective(fov_y_, aspect(), near_z_, far_z_);
}

Mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

Mat4 Camera::rotationMatrix() const {
    Vec3 r   = active_->right();
    Vec3 u   = active_->up();
    Vec3 fwd = active_->forward();

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

Vec3 Camera::forward() const { return active_->forward(); }
Vec3 Camera::right()   const { return active_->right(); }
Vec3 Camera::up()      const { return active_->up(); }

} // namespace mulan::engine
