/**
 * @file TurntableRotation.h
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

#include "RotationMode.h"

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

    void setOrbitSpeed(double s) override { m_orbitSpeed = s; }
    double orbitSpeed() const override { return m_orbitSpeed; }

    double yaw()   const override { return m_yaw; }
    double pitch() const override { return m_pitch; }
    void setYawPitch(double yaw, double pitch) override;

private:
    double m_yaw   = kPi * 0.25;
    double m_pitch = kPi * 0.33;
    double m_orbitSpeed = 0.005;

    int  m_orbitPrevX = 0;
    int  m_orbitPrevY = 0;
    bool m_orbitDrag  = false;

    static constexpr double kPi       = 3.14159265358979323846;
    static constexpr double kMaxPitch =  kPi * 0.5 - 0.01;
    static constexpr double kMinPitch = -kPi * 0.5 + 0.01;
};

} // namespace mulan::Engine
