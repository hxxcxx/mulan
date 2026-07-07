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

Camera::Camera(CameraMode initialMode) : mode_(initialMode) {
    createRotation(initialMode);
}

Camera::Camera(const Camera& other) {
    copyFrom(other);
}

Camera& Camera::operator=(const Camera& other) {
    if (this != &other) {
        copyFrom(other);
    }
    return *this;
}

void Camera::copyFrom(const Camera& other) {
    mode_ = other.mode_;
    createRotation(mode_);
    target_ = other.target_;
    pan_offset_ = other.pan_offset_;
    distance_ = other.distance_;
    width_ = other.width_;
    height_ = other.height_;
    fov_y_ = other.fov_y_;
    near_z_ = other.near_z_;
    far_z_ = other.far_z_;
    ortho_ = other.ortho_;
    ortho_size_ = other.ortho_size_;
    min_distance_ = other.min_distance_;
    pan_speed_ = other.pan_speed_;
    zoom_speed_ = other.zoom_speed_;
    active_->setOrbitSpeed(other.active_->orbitSpeed());
    if (mode_ == CameraMode::Turntable) {
        active_->setYawPitch(other.active_->yaw(), other.active_->pitch());
    } else {
        active_->setRotation(other.active_->rotation());
    }
}

void Camera::setMode(CameraMode mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    createRotation(mode);
}

// ============================================================
// 模式专用访问
// ============================================================

double Camera::yaw() const {
    return active_->yaw();
}
double Camera::pitch() const {
    return active_->pitch();
}
void Camera::setYawPitch(double yaw, double pitch) {
    active_->setYawPitch(yaw, pitch);
}

math::Quat Camera::rotation() const {
    return active_->rotation();
}
void Camera::setRotation(const math::Quat& q) {
    active_->setRotation(q);
}

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
    // 平移作为"视图空间偏移"，不移动 target_（旋转中心）。
    // 这样 orbit 始终绕模型中心，平移与旋转完全解耦。
    // pan_offset_ 累积在视图空间（x=右,y=上），viewMatrix 直接叠加到平移列。
    const double viewportH = std::max(1, height_);
    const double visibleHeight = ortho_ ? 2.0 * ortho_size_ : 2.0 * distance_ * std::tan(fov_y_ * 0.5);
    const double scale = visibleHeight / viewportH * pan_speed_;
    // "抓取场景"语义：鼠标右移(dx>0) → 场景跟随向右 → 视图空间 x 增大
    //                鼠标下移(dy>0) → 场景跟随向下 → 视图空间 y 减小
    pan_offset_.x += dx * scale;
    pan_offset_.y -= dy * scale;
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

void Camera::fitToBox(const math::AABB3& box, double padding) {
    target_ = box.center().asVec();
    pan_offset_ = { 0, 0, 0 };  // 重新适配时清除平移偏移
    double radius = (box.max - box.min).length() * 0.5;

    if (ortho_) {
        ortho_size_ = radius * padding;
        distance_ = radius * padding * 2.0;
    } else {
        double halfFov = fov_y_ * 0.5;
        distance_ = radius * padding / std::sin(halfFov);
    }

    if (distance_ < min_distance_)
        distance_ = radius * 5.0 + 1.0;

    near_z_ = distance_ * 0.01;
    far_z_ = distance_ * 10.0;
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

math::Vec3 Camera::eyePosition() const {
    return target_ - active_->forward() * distance_;
}

math::Mat4 Camera::viewMatrix() const {
    math::Vec3 r = active_->right();
    math::Vec3 u = active_->up();
    math::Vec3 fwd = active_->forward();
    math::Vec3 eye = target_ - fwd * distance_;

    math::Mat4 v(1.0);
    v[0][0] = r.x;
    v[1][0] = r.y;
    v[2][0] = r.z;
    v[3][0] = -r.dot(eye);
    v[0][1] = u.x;
    v[1][1] = u.y;
    v[2][1] = u.z;
    v[3][1] = -u.dot(eye);
    v[0][2] = -fwd.x;
    v[1][2] = -fwd.y;
    v[2][2] = -fwd.z;
    v[3][2] = fwd.dot(eye);
    v[0][3] = 0;
    v[1][3] = 0;
    v[2][3] = 0;
    v[3][3] = 1;

    // 叠加视图空间平移（pan）。在视图空间平移整个画面，
    // 不影响 eye 的 3D 位置（旋转中心仍是 target_）。
    v[3][0] += pan_offset_.x;
    v[3][1] += pan_offset_.y;
    v[3][2] += pan_offset_.z;
    return v;
}

math::Mat4 Camera::projectionMatrix() const {
    if (ortho_) {
        double h = ortho_size_;
        double w = h * aspect();
        return math::Mat4::ortho(-w, w, -h, h, near_z_, far_z_);
    }
    return math::Mat4::perspective(fov_y_, aspect(), near_z_, far_z_);
}

math::Mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

math::Mat4 Camera::rotationMatrix() const {
    math::Vec3 r = active_->right();
    math::Vec3 u = active_->up();
    math::Vec3 fwd = active_->forward();

    math::Mat4 rm(1.0);
    rm[0][0] = r.x;
    rm[1][0] = r.y;
    rm[2][0] = r.z;
    rm[3][0] = 0;
    rm[0][1] = u.x;
    rm[1][1] = u.y;
    rm[2][1] = u.z;
    rm[3][1] = 0;
    rm[0][2] = -fwd.x;
    rm[1][2] = -fwd.y;
    rm[2][2] = -fwd.z;
    rm[3][2] = 0;
    rm[0][3] = 0;
    rm[1][3] = 0;
    rm[2][3] = 0;
    rm[3][3] = 1;
    return rm;
}

math::Frustum3 Camera::frustum() const {
    return math::Frustum3::fromViewProjection(viewProjectionMatrix());
}

math::Vec3 Camera::forward() const {
    return active_->forward();
}
math::Vec3 Camera::right() const {
    return active_->right();
}
math::Vec3 Camera::up() const {
    return active_->up();
}

}  // namespace mulan::engine
