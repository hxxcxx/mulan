/**
 * @file Operator.h
 * @brief 交互操作器抽象基类
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - Operator 是纯虚基类，继承者通过覆盖事件方法参与输入处理
 *  - 返回 bool 表示事件是否已被消费（consumed）
 *  - 简洁设计：不使用侵入式引用计数，不使用 RTTI 宏
 *  - 上层 (Widget / Viewer) 通过 std::unique_ptr 管理生命周期
 */

#pragma once

#include "InputEvent.h"

namespace mulan::engine {

class Camera;

class Operator {
public:
    virtual ~Operator() = default;

    // --- 鼠标事件 ---

    virtual bool onMousePress      (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseRelease    (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseMove       (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseDoubleClick(const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onWheel           (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }

    // --- 键盘事件 ---

    virtual bool onKeyPress  (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onKeyRelease(const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }

    // --- 生命周期 ---

    virtual void onActivate  (Camera& cam) { (void)cam; }
    virtual void onDeactivate(Camera& cam) { (void)cam; }

    // --- 统一分发入口 ---

    bool handleEvent(const InputEvent& e, Camera& cam) {
        switch (e.type) {
        case InputEvent::Type::MousePress:       return onMousePress(e, cam);
        case InputEvent::Type::MouseRelease:     return onMouseRelease(e, cam);
        case InputEvent::Type::MouseMove:        return onMouseMove(e, cam);
        case InputEvent::Type::MouseDoubleClick: return onMouseDoubleClick(e, cam);
        case InputEvent::Type::Wheel:            return onWheel(e, cam);
        case InputEvent::Type::KeyPress:         return onKeyPress(e, cam);
        case InputEvent::Type::KeyRelease:       return onKeyRelease(e, cam);
        }
        return false;
    }
};

} // namespace mulan::Engine
