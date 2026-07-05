/**
 * @file camera.h
 * @brief 相机策略模式托管类 — 持有 RotationMode 实现，按模式创建
 *
 * 职责：
 *  - 投影参数（fov、near/far、ortho）
 *  - 轨道参数（target、distance）
 *  - pan / zoom / fitToBox 等与旋转无关的操作
 *  - 旋转操作全部委托给当前激活的 RotationMode
 *
 * 两种模式的旋转状态完全独立，切换时不做有损转换。
 * Z-up 坐标系。
 *
 * @author hxxcxx
 * @date 2026-04-25
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <mulan/math/math.h>
#include "rotation_mode.h"

namespace mulan::engine {

/// 相机旋转模式
enum class CameraMode : uint8_t {
    Turntable,   ///< yaw/pitch 转台，世界 Z-up 约束
    Trackball,   ///< 四元数自由旋转（arcball）
};

class Camera {
public:
    /// @param initialMode 初始旋转模式，缺省为 Trackball
    explicit Camera(CameraMode initialMode = CameraMode::Trackball);

    // ==================== 模式控制 ====================

    CameraMode mode() const { return mode_; }
    void setMode(CameraMode mode);

    // ==================== 视口 ====================

    void setViewport(int width, int height) {
        width_ = width;
        height_ = height;
    }

    int width()  const { return width_; }
    int height() const { return height_; }
    double aspect() const { return height_ > 0 ? double(width_) / height_ : 1.0; }

    // ==================== 投影参数 ====================

    void setFieldOfView(double fovY) { fov_y_ = fovY; }
    void setClipPlanes(double nearZ, double farZ) { near_z_ = nearZ; far_z_ = farZ; }
    void setOrthographic(bool ortho) { ortho_ = ortho; }

    double fieldOfView() const { return fov_y_; }
    bool   isOrthographic() const { return ortho_; }
    double nearPlane() const { return near_z_; }
    double farPlane()  const { return far_z_; }

    // ==================== 轨道参数 ====================

    void setTarget(const math::Vec3& target) { target_ = target; }
    const math::Vec3& target() const { return target_; }

    void setDistance(double dist) { distance_ = dist; }
    double distance() const { return distance_; }

    double orthoSize() const { return ortho_size_; }
    void setOrthoSize(double s) { ortho_size_ = s; }

    // ==================== 模式专用访问 ====================

    /// Turntable 专用：yaw 角度（仅在 Turntable 模式下有意义）
    double yaw()   const;
    /// Turntable 专用：pitch 角度（仅在 Turntable 模式下有意义）
    double pitch() const;
    /// Turntable 专用：设置 yaw/pitch
    void setYawPitch(double yaw, double pitch);

    /// Trackball 专用：四元数旋转（仅在 Trackball 模式下有意义）
    math::Quat rotation() const;
    /// Trackball 专用：设置四元数旋转
    void setRotation(const math::Quat& q);

    // ==================== 交互 ====================

    /// Turntable: delta-based orbit；Trackball: 四元数 delta 旋转
    void orbitDelta(double dx, double dy);

    /// 兼容接口，等效 orbitDelta
    void orbit(double dx, double dy) { orbitDelta(dx, dy); }

    /// Trackball arcball: 开始旋转
    void beginOrbit(int x, int y);
    /// Trackball arcball: 旋转到指定屏幕坐标
    void orbitToPoint(int x, int y);
    /// Trackball arcball: 结束旋转
    void endOrbit();

    void pan(double dx, double dy);
    void zoom(double delta);
    void fitToBox(const math::AABB3& box, double padding = 1.2);

    // ==================== 速度参数 ====================

    void setOrbitSpeed(double s);
    double orbitSpeed() const;

    void setPanSpeed(double s)   { pan_speed_ = s; }
    void setZoomSpeed(double s)  { zoom_speed_ = s; }

    double panSpeed()  const { return pan_speed_; }
    double zoomSpeed() const { return zoom_speed_; }

    // ==================== 矩阵计算 ====================

    math::Vec3 eyePosition() const;
    math::Mat4 viewMatrix() const;
    math::Mat4 projectionMatrix() const;
    math::Mat4 viewProjectionMatrix() const;
    math::Mat4 rotationMatrix() const;
    math::Frustum3 frustum() const;

    // ==================== 方向向量 ====================

    math::Vec3 forward() const;
    math::Vec3 right() const;
    math::Vec3 up() const;

    // ==================== 3D 拾取 ====================

    /// 从屏幕像素坐标生成世界空间射线
    /// @param screenX  像素 X（左上角为原点）
    /// @param screenY  像素 Y（左上角为原点）
    math::Ray3 screenRay(int screenX, int screenY) const {
        // 屏幕 → NDC
        double ndcX =  (2.0 * screenX) / width_  - 1.0;
        double ndcY = -(2.0 * screenY) / height_ + 1.0;

        // NDC 近/远裁剪面点
        math::Vec4 nearPt(ndcX, ndcY, -1.0, 1.0);
        math::Vec4 farPt (ndcX, ndcY,  1.0, 1.0);

        // 逆 VP 变换到世界空间
        math::Mat4 invVP = viewProjectionMatrix().inverse();

        math::Vec4 nearWorld = invVP * nearPt;
        nearWorld /= nearWorld.w;
        math::Vec4 farWorld = invVP * farPt;
        farWorld /= farWorld.w;

        math::Point3 origin(nearWorld.x, nearWorld.y, nearWorld.z);
        math::Vec3 dir = (math::Vec3(farWorld) - origin.asVec()).normalized();
        return math::Ray3(origin, dir);
    }

private:
    /// 根据模式创建对应的 RotationMode 实例
    void createRotation(CameraMode mode);

    CameraMode mode_ = CameraMode::Trackball;

    std::unique_ptr<RotationMode> active_;

    math::Vec3   target_   = {0, 0, 0};     ///< 轨道旋转中心（模型中心，pan 不改它）
    math::Vec3   pan_offset_ = {0, 0, 0};   ///< 视图空间平移偏移（与旋转中心解耦）
    double distance_ = 10.0;

    // 投影参数
    int    width_    = 800;
    int    height_   = 600;
    double fov_y_     = 3.14159265358979323846 / 4.0;
    double near_z_    = 0.1;
    double far_z_     = 1000.0;
    bool   ortho_    = true;
    double ortho_size_ = 5.0;

    // 交互速度
    double pan_speed_    = 0.003;
    double zoom_speed_   = 1.08;
    double min_distance_ = 0.001;
};

} // namespace mulan::engine
