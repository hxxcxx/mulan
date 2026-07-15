/**
 * @file camera_manipulator.h
 * @brief 组合相机操作器 — 集成轨道旋转、平移、缩放
 * @author hxxcxx
 * @date 2026-04-17 (原始) / 2026-07-14 (按钮记录修复) / 2026-07-15 (返回 bool)
 *
 * 设计思路：
 *  - 一个类完成所有基础相机交互，无需拆分为多个小类
 *  - 默认映射：左键轨道旋转、中键平移、右键平移、滚轮缩放
 *  - 按键映射可通过 Config 调整
 *  - 速度因子暴露为公共成员便于调参
 *
 * 2026-07-14 修复：
 *  - onMousePress 不再对任意按钮置 dragging_，只接受导航按钮（orbit/pan）；
 *  - 记录启动按钮 press_button_，onMouseRelease 只结束匹配按钮，避免多按钮错序；
 *  - 非 navigation 事件返回 false，让上层路由给其他 handler。
 */

#pragma once

#include "operator.h"
#include "../render/camera/camera.h"

namespace mulan::engine {

class CameraManipulator : public Operator {
public:
    // --- 行为配置 ---

    struct Config {
        MouseButton orbitButton = MouseButton::Left;
        MouseButton panButton = MouseButton::Middle;
        MouseButton panAltButton = MouseButton::Right;  ///< 可选的第二平移按钮

        double zoomFactor = 1.0;                        ///< 每滚轮档位缩放百分比
    };

    Config config;

    bool isDragging() const { return dragging_; }

    // --- Operator 接口实现 ---

    bool onMousePress(const InputEvent& e, Camera& cam) override {
        const bool isOrbit = e.button & config.orbitButton;
        const bool isPan = e.button & (config.panButton | config.panAltButton);
        if (!isOrbit && !isPan) {
            return false;  // 非导航按钮：不抢占，交回路由
        }

        last_x_ = e.x;
        last_y_ = e.y;
        dragging_ = true;
        press_button_ = e.button;  // 记录启动按钮，供 release 匹配

        if (isOrbit) {
            cam.beginOrbit(e.x, e.y);
        }
        return true;
    }

    bool onMouseRelease(const InputEvent& e, Camera& cam) override {
        if (!dragging_) {
            return false;
        }
        // 只结束匹配启动按钮的 release，多按钮错序不会提前终止其他交互
        if (!(e.button & press_button_)) {
            return false;
        }
        cam.endOrbit();
        dragging_ = false;
        press_button_ = MouseButton::None;
        return true;
    }

    bool onMouseMove(const InputEvent& e, Camera& cam) override {
        if (!dragging_)
            return false;

        int dx = e.x - last_x_;
        int dy = e.y - last_y_;
        last_x_ = e.x;
        last_y_ = e.y;

        if (dx == 0 && dy == 0)
            return false;

        // --- 轨道旋转 ---
        if (e.isButtonPressed(config.orbitButton)) {
            cam.orbitToPoint(e.x, e.y);
            return true;
        }

        // --- 平移（中键 或 右键） ---
        if (e.isButtonPressed(config.panButton) || e.isButtonPressed(config.panAltButton)) {
            cam.pan(static_cast<double>(dx), static_cast<double>(dy));
            return true;
        }

        return false;
    }

    bool onWheel(const InputEvent& e, Camera& cam) override {
        double delta = static_cast<double>(e.wheelDelta) * config.zoomFactor;
        cam.zoom(-delta);
        return true;
    }

    bool onKeyPress(const InputEvent& e, Camera& cam) override {
        // Home 键（或 F 键）：适配全场景
        // 子类可扩展更多快捷键
        (void) e;
        (void) cam;
        return false;
    }

    void onActivate(Camera& cam) override { active_camera_ = &cam; }

    void onDeactivate(Camera& cam) override {
        if (dragging_) {
            cam.endOrbit();
        }
        dragging_ = false;
        press_button_ = MouseButton::None;
        active_camera_ = nullptr;
    }

    bool onCancel() override {
        // 焦点丢失 / 系统取消时清理 drag 状态（幂等）
        const bool wasDragging = dragging_;
        if (wasDragging && active_camera_) {
            active_camera_->endOrbit();
        }
        dragging_ = false;
        press_button_ = MouseButton::None;
        return wasDragging;
    }

private:
    int last_x_ = 0;
    int last_y_ = 0;
    bool dragging_ = false;
    MouseButton press_button_ = MouseButton::None;  ///< 启动本次 drag 的按钮
    Camera* active_camera_ = nullptr;               ///< 仅在 Active 生命周期内有效

protected:
    InputDisposition handledDisposition() const override { return InputDisposition::ViewNavigation; }
};

}  // namespace mulan::engine
