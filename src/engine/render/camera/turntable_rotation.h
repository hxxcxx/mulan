/**
 * @file turntable_rotation.h
 * @brief Turntable 旋转模式 — yaw/pitch 角度驱动，Z-up 约束
 *
 * 适合机械类查看场景：
 *  - 球面坐标 (cos(pitch)*cos(yaw), cos(pitch)*sin(yaw), sin(pitch))
 *  - pitch 限制在 ±89°，防止翻转
 *  - 使用 delta 增量进行轨道旋转
 *
 * @author hxxcxx
 * @date 2026-04-25
 */

#pragma once

#include "rotation_mode.h"

namespace mulan::engine {

class TurntableRotation : public RotationMode {
public:
    TurntableRotation();

    Vec3 forward() const override;
    Vec3 right()   const override;
    Vec3 up()      const override;

    void orbitDelta(double dx, double dy) override;

    void beginOrbit(int x, int y, int viewW, int viewH) override;
    void orbitToPoint(int x, int y, int viewW, int viewH) override;
    void endOrbit() override;

    void setOrbitSpeed(double s) override { orbit_speed_ = s; }
    double orbitSpeed() const override { return orbit_speed_; }

    double yaw()   const override { return yaw_; }
    double pitch() const override { return pitch_; }
    void setYawPitch(double yaw, double pitch) override;

private:
    double yaw_   = kPi * 0.25;
    double pitch_ = kPi * 0.33;
    double orbit_speed_ = 0.005;

    int  orbit_prev_x_ = 0;
    int  orbit_prev_y_ = 0;
    bool orbit_drag_  = false;

    static constexpr double kPi       = 3.14159265358979323846;
    static constexpr double kMaxPitch =  kPi * 0.5 - 0.01;
    static constexpr double kMinPitch = -kPi * 0.5 + 0.01;
};

} // namespace mulan::engine
