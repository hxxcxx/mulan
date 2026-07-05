/**
 * @file trackball_rotation.h
 * @brief Trackball 旋转模式 — 四元数自由旋转（arcball）
 *
 * 适合任意方向查看：
 *  - 由四元数 rotation_ 完全控制朝向
 *  - 方向向量通过四元数左乘基向量计算（forward=+Y, right=+X, up=+Z）
 *  - 交互使用 arcball 虚拟球面投影，手感自然
 *  - 无俯仰角限制，可任意方向旋转
 *
 * @author hxxcxx
 * @date 2026-04-25
 */

#pragma once

#include "rotation_mode.h"

namespace mulan::engine {

class TrackballRotation : public RotationMode {
public:
    TrackballRotation();

    math::Vec3 forward() const override;
    math::Vec3 right() const override;
    math::Vec3 up() const override;

    void orbitDelta(double dx, double dy) override;

    void beginOrbit(int x, int y, int viewW, int viewH) override;
    void orbitToPoint(int x, int y, int viewW, int viewH) override;
    void endOrbit() override;

    void setOrbitSpeed(double s) override { arcball_speed_ = s; }
    double orbitSpeed() const override { return arcball_speed_; }

    math::Quat rotation() const override { return rotation_; }
    void setRotation(const math::Quat& q) override { rotation_ = q.normalized(); }

private:
    math::Vec3 arcballProject(int x, int y, int viewW, int viewH) const;

    math::Quat rotation_;
    math::Vec3 arcball_prev_ = { 0, 0, 0 };
    bool arcball_active_ = false;
    double arcball_speed_ = 1.75;
};

}  // namespace mulan::engine
