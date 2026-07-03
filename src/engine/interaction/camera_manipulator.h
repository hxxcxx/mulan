/**
 * @file camera_manipulator.h
 * @brief 组合相机操作器 — 集成轨道旋转、平移、缩放
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - 一个类完成所有基础相机交互，无需拆分为多个小类
 *  - 默认映射：左键轨道旋转、中键平移、右键平移、滚轮缩放
 *  - 按键映射可通过 Config 调整
 *  - 速度因子暴露为公共成员便于调参
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
        MouseButton panButton   = MouseButton::Middle;
        MouseButton panAltButton = MouseButton::Right;   ///< 可选的第二平移按钮

        double zoomFactor   = 1.0;     ///< 每滚轮档位缩放百分比
        double minDistance   = 0.001;
    };

    Config config;

    // --- Operator 接口实现 ---

    bool onMousePress(const InputEvent& e, Camera& cam) override {
        last_x_ = e.x;
        last_y_ = e.y;
        dragging_ = true;

        if (e.isButtonPressed(config.orbitButton)) {
            cam.beginOrbit(e.x, e.y);
        }
        return true;
    }

    bool onMouseRelease(const InputEvent& e, Camera& cam) override {
        (void)e;
        cam.endOrbit();
        dragging_ = false;
        return true;
    }

    bool onMouseMove(const InputEvent& e, Camera& cam) override {
        if (!dragging_) return false;

        int dx = e.x - last_x_;
        int dy = e.y - last_y_;
        last_x_ = e.x;
        last_y_ = e.y;

        if (dx == 0 && dy == 0) return false;

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
        (void)e; (void)cam;
        return false;
    }

private:
    int  last_x_    = 0;
    int  last_y_    = 0;
    bool dragging_ = false;
};

} // namespace mulan::engine
