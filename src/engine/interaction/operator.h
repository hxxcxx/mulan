/**
 * @file operator.h
 * @brief 交互操作器抽象基类 — 显式状态机 + 完成回调
 * @author hxxcxx
 * @date 2026-04-17 (原始) / 2026-06-29 (状态机重构)
 *
 * 设计思路：
 *  - Operator 是纯虚基类，继承者通过覆盖事件方法参与输入处理。
 *  - 返回 bool 表示事件是否已被消费（consumed）。
 *  - 显式状态机：Inactive → Active → (Finished | Cancelled)。
 *    由子类在事件中调用 finish() 结束交互，状态完全由 Operator 自身决定，
 *    不再有"宿主阻塞循环覆盖状态"的问题（重构前的 #3 bug）。
 *  - finish() 只翻状态、不触发回调；回调由 Viewport 在 handleInput 返回后
 *    检测到 isFinished() 时于 Operator 外部触发，避免在 Operator 自身成员
 *    函数里析构自身（delete-this UB）。
 *  - 完成通知通过 std::function 回调，engine 层不依赖任何 UI 框架。
 *  - 上层（Viewport）以 LIFO 栈管理生命周期，模态交互 push/pop。
 *  - 不使用侵入式引用计数，不使用 RTTI 宏。
 *
 * 两种典型子类：
 *  - CameraManipulator：默认相机操控（轨道/平移/缩放），常驻 Viewport 栈底。
 *  - 模态工具 Operator（如拾取）：push 入栈 → 处理事件 → finish() → 自动 pop。
 *    模态工具可覆盖 handleCameraEvent() 让中键/右键/滚轮在交互期间仍操控相机。
 */
#pragma once

#include "input_event.h"

#include <functional>

namespace mulan::engine {

class Camera;

class Operator {
public:
    /// 交互状态
    enum class State : uint8_t {
        Inactive  = 0,   ///< 未激活（构造后、push 前）
        Active    = 1,   ///< 激活中（在栈顶接收事件）
        Finished  = 2,   ///< 正常完成（finish(true)）
        Cancelled = 3,   ///< 用户取消（finish(false)，ESC / 右键）
    };

    /// 完成回调签名：参数为该 Operator 自身（调用方可读取结果、状态）
    using FinishCallback = std::function<void(Operator&)>;

    virtual ~Operator() = default;

    // ============================================================
    // 状态机
    // ============================================================

    State state() const { return state_; }
    bool isActive()    const { return state_ == State::Active; }
    bool isFinished()  const { return state_ == State::Finished || state_ == State::Cancelled; }
    bool isCompleted() const { return state_ == State::Finished; }

    /// 注册完成回调（由 Viewport 在 push 时自动连接到 popOperator）
    /// 在 Operator 完成后、由 Viewport 于 Operator 外部触发（非 finish() 内）。
    void onFinish(FinishCallback cb) { on_finish_ = std::move(cb); }

    // ============================================================
    // 宿主（Viewport）管理接口
    //
    // 这些方法供 Viewport 在 push/pop 时切换状态、读取完成回调。
    // 普通业务代码不应直接调用。
    // ============================================================

    /// 由宿主设置状态（push→Active、pop→Inactive）
    void setState(State s) { state_ = s; }
    /// 由宿主读取完成回调（检测到 isFinished 后于 Operator 外部触发）
    const FinishCallback& finishHook() const { return on_finish_; }

    // ============================================================
    // 鼠标事件（子类覆盖）
    // ============================================================

    virtual bool onMousePress      (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseRelease    (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseMove       (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onMouseDoubleClick(const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onWheel           (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }

    // ============================================================
    // 键盘事件（子类覆盖）
    // ============================================================

    virtual bool onKeyPress  (const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }
    virtual bool onKeyRelease(const InputEvent& e, Camera& cam) { (void)e; (void)cam; return false; }

    // ============================================================
    // 生命周期
    // ============================================================

    virtual void onActivate  (Camera& cam) { (void)cam; }
    virtual void onDeactivate(Camera& cam) { (void)cam; }

    // ============================================================
    // 统一分发入口（virtual —— 修复重构前的 #2 bug）
    //
    // 调度顺序：
    //   1. 若子类覆盖了 handleCameraEvent，且该事件被判定为相机事件，
    //      先交给它处理（模态工具可借此让中键/右键/滚轮始终操控相机）。
    //   2. 未消费则按 type 分发给 onMouse*/onKey* 等具体处理。
    // ============================================================

    virtual bool handleEvent(const InputEvent& e, Camera& cam) {
        if (!isActive()) return false;

        // 1. 相机事件拦截钩子（默认实现返回 false，见下）
        if (isCameraEvent(e) && handleCameraEvent(e, cam))
            return true;

        // 2. 常规分发
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

protected:
    // ============================================================
    // 子类工具
    // ============================================================

    /// 结束交互。completed=true → Finished；false → Cancelled。
    /// 仅翻转状态，不触发回调（回调由 Viewport 在 handleInput 之后外部触发，
    /// 避免在 Operator 成员函数内析构自身）。重复调用安全（幂等）。
    void finish(bool completed = true) {
        if (state_ == State::Finished || state_ == State::Cancelled) return;
        state_ = completed ? State::Finished : State::Cancelled;
    }

    /// 判定是否为相机操控事件（中键 / 右键 / 滚轮）。
    /// 模态工具可覆盖以自定义相机事件范围。
    virtual bool isCameraEvent(const InputEvent& e) const {
        (void)e;
        return false;  // 默认无相机事件；CameraManipulator 自己处理一切
    }

    /// 相机事件处理器。默认空实现；模态工具通常会组合一个 CameraManipulator
    /// 并在此转发给它（参考注释中的示例）。
    virtual bool handleCameraEvent(const InputEvent& e, Camera& cam) {
        (void)e; (void)cam;
        return false;
    }

    State          state_    = State::Inactive;
    FinishCallback on_finish_;
};

} // namespace mulan::engine
