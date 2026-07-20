/**
 * @file trackball_rotation.h
 * @brief Trackball 旋转模式 — 四元数屏幕空间轨道旋转
 *
 * 适合任意方向查看：
 *  - 由四元数 rotation_ 完全控制朝向
 *  - 方向向量通过四元数左乘基向量计算（forward=+Y, right=+X, up=+Z）
 *  - 鼠标水平/垂直位移分别映射到当前视图 up/right 轴
 *  - 无俯仰角限制，可任意方向旋转，且不会由鼠标屏幕位置额外引入 roll
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

    void setOrbitSpeed(double s) override { orbit_speed_ = s; }
    double orbitSpeed() const override { return orbit_speed_; }

    math::Quat rotation() const override { return rotation_; }
    void setRotation(const math::Quat& q) override { rotation_ = q.normalized(); }

private:
    void applyScreenOrbit(double dx, double dy, int viewW, int viewH);

    math::Quat rotation_;
    int orbit_prev_x_ = 0;
    int orbit_prev_y_ = 0;
    bool orbit_drag_ = false;
    double orbit_speed_ = 1.75;
};

}  // namespace mulan::engine
