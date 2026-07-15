#include "camera.h"
#include "turntable_rotation.h"
#include "trackball_rotation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mulan::engine {
namespace {

bool finitePoint(const math::Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool finiteVector(const math::Vec3& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

bool finiteQuaternion(const math::Quat& rotation) {
    return std::isfinite(rotation.w) && std::isfinite(rotation.x) && std::isfinite(rotation.y) &&
           std::isfinite(rotation.z) && rotation.normSq() > std::numeric_limits<double>::epsilon();
}

bool finiteBounds(const math::AABB3& box) {
    return finitePoint(box.min) && finitePoint(box.max);
}

bool finiteSphere(const math::Sphere3& sphere) {
    return sphere.isValid() && finitePoint(sphere.center) && std::isfinite(sphere.radius);
}

}  // namespace

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
    pan_speed_ = other.pan_speed_;
    zoom_speed_ = other.zoom_speed_;
    depth_revision_ = other.depth_revision_;
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
    markDepthChanged();
}

void Camera::setViewport(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
}

void Camera::setFieldOfView(double fovY) {
    if (std::isfinite(fovY) && fovY > 0.001 && fovY < math::kPi - 0.001) {
        fov_y_ = fovY;
    }
}

void Camera::setClipPlanes(double nearZ, double farZ) {
    if (!std::isfinite(nearZ) || !std::isfinite(farZ) || nearZ < kMinNearPlane || farZ < nearZ + kMinClipSpan) {
        return;
    }
    near_z_ = nearZ;
    far_z_ = farZ;
}

void Camera::setTarget(const math::Vec3& target) {
    if (!finiteVector(target)) {
        return;
    }
    target_ = target;
    markDepthChanged();
}

void Camera::setDistance(double dist) {
    if (!std::isfinite(dist) || dist < kMinOrbitDistance) {
        return;
    }
    distance_ = dist;
    markDepthChanged();
}

void Camera::setOrthoSize(double size) {
    if (std::isfinite(size) && size >= kMinOrthoSize) {
        ortho_size_ = size;
    }
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
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) {
        return;
    }
    active_->setYawPitch(yaw, pitch);
    markDepthChanged();
}

math::Quat Camera::rotation() const {
    return active_->rotation();
}
void Camera::setRotation(const math::Quat& q) {
    if (!finiteQuaternion(q)) {
        return;
    }
    active_->setRotation(q);
    markDepthChanged();
}

// ============================================================
// 交互
// ============================================================

void Camera::orbitDelta(double dx, double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return;
    }
    active_->orbitDelta(dx, dy);
    markDepthChanged();
}

void Camera::beginOrbit(int x, int y) {
    active_->beginOrbit(x, y, width_, height_);
}

void Camera::orbitToPoint(int x, int y) {
    active_->orbitToPoint(x, y, width_, height_);
    markDepthChanged();
}

void Camera::endOrbit() {
    active_->endOrbit();
}

void Camera::pan(double dx, double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return;
    }
    // 平移作为"视图空间偏移"，不移动 target_（旋转中心）。
    // 这样 orbit 始终绕模型中心，平移与旋转完全解耦。
    // pan_offset_ 累积在视图空间（x=右,y=上），viewMatrix 直接叠加到平移列。
    const double viewportH = std::max(1, height_);
    const double visibleHeight = ortho_ ? 2.0 * ortho_size_ : 2.0 * distance_ * std::tan(fov_y_ * 0.5);
    const double scale = visibleHeight / viewportH * pan_speed_;
    // "抓取场景"语义：鼠标右移(dx>0) → 场景跟随向右 → 视图空间 x 增大
    //                鼠标下移(dy>0) → 场景跟随向下 → 视图空间 y 减小
    const double nextX = pan_offset_.x + dx * scale;
    const double nextY = pan_offset_.y - dy * scale;
    if (std::isfinite(nextX) && std::isfinite(nextY)) {
        pan_offset_.x = nextX;
        pan_offset_.y = nextY;
    }
}

void Camera::zoom(double delta) {
    zoomAt(delta, static_cast<double>(width_) * 0.5, static_cast<double>(height_) * 0.5);
}

void Camera::zoomAt(double delta, double screenX, double screenY) {
    if (!std::isfinite(delta) || !std::isfinite(screenX) || !std::isfinite(screenY)) {
        return;
    }

    const double factor = std::pow(zoom_speed_, delta);
    if (!std::isfinite(factor)) {
        return;
    }

    const double ndcX = 2.0 * screenX / static_cast<double>(width_) - 1.0;
    const double ndcY = 1.0 - 2.0 * screenY / static_cast<double>(height_);
    const double oldHalfHeight = ortho_ ? ortho_size_ : distance_ * std::tan(fov_y_ * 0.5);
    double nextHalfHeight = oldHalfHeight;
    double nextOrthoSize = ortho_size_;
    double nextDistance = distance_;

    if (ortho_) {
        nextOrthoSize = std::max(ortho_size_ * factor, kMinOrthoSize);
        nextHalfHeight = nextOrthoSize;
    } else {
        nextDistance = std::max(distance_ * factor, kMinOrbitDistance);
        nextHalfHeight = nextDistance * std::tan(fov_y_ * 0.5);
    }

    // 缩放改变光标处对应的视图平面坐标；用等量 pan 补偿，使该锚点保持在原像素。
    const double halfHeightDelta = nextHalfHeight - oldHalfHeight;
    const double nextPanX = pan_offset_.x + ndcX * halfHeightDelta * aspect();
    const double nextPanY = pan_offset_.y + ndcY * halfHeightDelta;
    if (!std::isfinite(nextOrthoSize) || !std::isfinite(nextDistance) || !std::isfinite(nextPanX) ||
        !std::isfinite(nextPanY)) {
        return;
    }

    pan_offset_.x = nextPanX;
    pan_offset_.y = nextPanY;
    if (ortho_) {
        ortho_size_ = nextOrthoSize;
    } else {
        distance_ = nextDistance;
        markDepthChanged();
    }
}

void Camera::fitToBox(const math::AABB3& box, double padding) {
    fitToSphere(math::Sphere3::fromAABB(box), padding);
}

void Camera::fitToSphere(const math::Sphere3& sphere, double padding) {
    if (!finiteSphere(sphere) || !std::isfinite(padding) || padding <= 0.0) {
        return;
    }

    const double radius = std::max(sphere.radius, kMinOrbitDistance);
    double nextOrthoSize = ortho_size_;
    double nextDistance = distance_;

    if (ortho_) {
        // ortho_size_ 是垂直半高；窄视口需要按横向约束放大，才能完整容纳包围球。
        nextOrthoSize = radius * padding / std::min(1.0, aspect());
        nextDistance = radius * padding * 2.0;
    } else {
        const double verticalHalfFov = fov_y_ * 0.5;
        const double horizontalHalfFov = std::atan(std::tan(verticalHalfFov) * aspect());
        const double halfFov = std::min(verticalHalfFov, horizontalHalfFov);
        nextDistance = radius * padding / std::sin(halfFov);
    }

    if (!std::isfinite(nextOrthoSize) || !std::isfinite(nextDistance) || nextOrthoSize < kMinOrthoSize ||
        nextDistance < kMinOrbitDistance) {
        return;
    }

    target_ = sphere.center.asVec();
    pan_offset_ = { 0, 0, 0 };  // 重新适配时清除平移偏移
    ortho_size_ = nextOrthoSize;
    distance_ = nextDistance;
    markDepthChanged();
    fitClipPlanesToSphere(sphere, padding);
}

void Camera::fitClipPlanesToBox(const math::AABB3& box, double padding, ClipPlaneFitMode mode) {
    if (box.isEmpty() || !finiteBounds(box) || !std::isfinite(padding)) {
        return;
    }

    const math::Vec3 eye = eyePosition();
    const math::Vec3 fwd = forward();
    double minDepth = std::numeric_limits<double>::max();
    double maxDepth = -std::numeric_limits<double>::max();

    for (int i = 0; i < 8; ++i) {
        const math::Point3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                                  (i & 4) ? box.max.z : box.min.z);
        const double depth = (corner.asVec() - eye).dot(fwd);
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }

    if (!std::isfinite(minDepth) || !std::isfinite(maxDepth)) {
        return;
    }

    const double radius = std::max((box.max - box.min).length() * 0.5, kMinOrbitDistance);
    const double pad = std::max(1.0, padding);
    const double margin = std::max(radius * (pad - 1.0), kMinClipSpan);
    const double nearZ = std::max(kMinNearPlane, minDepth - margin);
    const double farZ = std::max(nearZ + kMinClipSpan, maxDepth + margin);
    applyFittedClipPlanes(nearZ, farZ, mode);
}

void Camera::fitClipPlanesToSphere(const math::Sphere3& sphere, double padding, ClipPlaneFitMode mode) {
    if (!finiteSphere(sphere) || !std::isfinite(padding)) {
        return;
    }

    // 裁剪面垂直于视线，必须使用沿前向的投影深度。欧氏距离会把离轴小图元
    // 误判得更远，使 near 越过图元并造成整个场景突然消失。
    const double centerDepth = (sphere.center.asVec() - eyePosition()).dot(forward());
    const double radius = std::max(sphere.radius, kMinOrbitDistance);
    const double margin = std::max(radius * (std::max(1.0, padding) - 1.0), kMinClipSpan);
    if (!std::isfinite(centerDepth) || !std::isfinite(radius) || !std::isfinite(margin)) {
        return;
    }

    const double nearZ = std::max(kMinNearPlane, centerDepth - radius - margin);
    const double farZ = std::max(nearZ + kMinClipSpan, centerDepth + radius + margin);
    applyFittedClipPlanes(nearZ, farZ, mode);
}

void Camera::applyFittedClipPlanes(double nearZ, double farZ, ClipPlaneFitMode mode) {
    if (mode == ClipPlaneFitMode::ExpandOnly) {
        nearZ = std::min(near_z_, nearZ);
        farZ = std::max(far_z_, farZ);
    }
    setClipPlanes(nearZ, farZ);
}

// ============================================================
// 速度参数
// ============================================================

void Camera::setOrbitSpeed(double s) {
    if (std::isfinite(s) && s > 0.0) {
        active_->setOrbitSpeed(s);
    }
}

double Camera::orbitSpeed() const {
    return active_->orbitSpeed();
}

void Camera::setPanSpeed(double speed) {
    if (std::isfinite(speed) && speed > 0.0) {
        pan_speed_ = speed;
    }
}

void Camera::setZoomSpeed(double speed) {
    if (std::isfinite(speed) && speed > 0.0) {
        zoom_speed_ = speed;
    }
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
