/**
 * @file rotation_mode.h
 * @brief 相机旋转模式抽象接口 — Strategy 模式
 *
 * 定义了旋转模式必须实现的操作：
 *  - 方向向量（forward / right / up）
 *  - 轨道旋转（orbitDelta）
 *  - 鼠标拖拽旋转生命周期（begin / orbitToPoint / end）
 *  - 速度参数
 *
 * @author hxxcxx
 * @date 2026-04-25
 */

#pragma once

#include <mulan/math/math.h>

namespace mulan::engine {

class RotationMode {
public:
    virtual ~RotationMode() = default;

    virtual math::Vec3 forward() const = 0;
    virtual math::Vec3 right() const = 0;
    virtual math::Vec3 up() const = 0;

    virtual void orbitDelta(double dx, double dy) = 0;

    virtual void beginOrbit(int x, int y, int viewW, int viewH) = 0;
    virtual void orbitToPoint(int x, int y, int viewW, int viewH) = 0;
    virtual void endOrbit() = 0;

    virtual void setOrbitSpeed(double s) = 0;
    virtual double orbitSpeed() const = 0;

    // ==================== 模式专用访问器（默认空实现，子类按需覆盖） ====================

    /// Turntable 专用
    virtual double yaw() const { return 0.0; }
    virtual double pitch() const { return 0.0; }
    virtual void setYawPitch(double /*yaw*/, double /*pitch*/) {}

    /// Trackball 专用
    virtual math::Quat rotation() const { return math::Quat{ 1, 0, 0, 0 }; }
    virtual void setRotation(const math::Quat& /*q*/) {}
};

}  // namespace mulan::engine
