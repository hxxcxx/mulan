/**
 * @file operator.h
 * @brief 交互操作器抽象基类 — 显式状态机 + 完成回调
 * @author hxxcxx
 * @date 2026-04-17 (原始) / 2026-06-29 (状态机重构) / 2026-07-15 (返回 bool 精简)
 *
 * 设计思路：
 *  - Operator 是纯虚基类，继承者通过覆盖事件方法参与输入处理。
 *  - 事件方法返回 bool：true 表示已消费（handled），false 表示忽略（交由下层）。
 *  - 显式状态机：Inactive → Active → (Finished | Cancelled)。
 *    由子类在事件中调用 finish() 结束交互，状态完全由 Operator 自身决定。
 *  - finish() 只翻状态、不触发回调；回调由 Viewport 在 handleInput 返回后
 *    检测到 isFinished() 时于 Operator 外部触发，避免在 Operator 自身成员
 *    函数里析构自身（delete-this UB）。
 *  - 上层（Viewport）以 LIFO 栈管理生命周期，模态交互 push/pop。
 *  - engine 层不依赖任何 UI 框架，不认识编辑语义（DocumentOperation 等）。
 *
 * 两种典型子类：
 *  - CameraManipulator：默认相机操控（轨道/平移/缩放），常驻 Viewport 栈底。
 *  - 模态工具 Operator：push 入栈 → 处理事件 → finish() → 自动 pop。
 *    模态工具可覆盖 handleCameraEvent() 让中键/右键/滚轮在交互期间仍操控相机。
 *
 * 2026-07-15：曾尝试用 HandlerResult（disposition + capture + invalidation 位）取代 bool，
 * 但 capture/invalidation 字段从未被消费，属于死代码。现回退到 bool，需要时再加。
 */
#pragma once

#include "input_event.h"

#include <functional>

namespace mulan::engine {

class Camera;

/// 取消原因：取消是幂等动作，但携带原因有助于日志与工具策略。
enum class CancelReason : uint8_t {
    System,          ///< PointerCancel / UngrabMouse / WindowDeactivate / 控件销毁
    FocusLost,       ///< 视口失去焦点
    DocumentSwitch,  ///< 文档切换 / 关闭
    Escape,          ///< 用户按 Escape
    RightClick,      ///< 用户右键取消
    Replaced,        ///< 被新交互替换
};

class Operator {
public:
    /// 交互状态
    enum class State : uint8_t {
        Inactive = 0,   ///< 未激活（构造后、push 前）
        Active = 1,     ///< 激活中（在栈顶接收事件）
        Finished = 2,   ///< 正常完成（finish(true)）
        Cancelled = 3,  ///< 用户取消（finish(false)，ESC / 右键）
    };

    /// 完成回调签名：参数为该 Operator 自身（调用方可读取结果、状态）
    using FinishCallback = std::function<void(Operator&)>;

    virtual ~Operator() = default;

    // ============================================================
    // 状态机
    // ============================================================

    State state() const { return state_; }
    bool isActive() const { return state_ == State::Active; }
    bool isFinished() const { return state_ == State::Finished || state_ == State::Cancelled; }
    bool isCompleted() const { return state_ == State::Finished; }

    /// 注册完成回调（由 Viewport 在 push 时自动连接到 popOperator）
    /// 在 Operator 完成后、由 Viewport 于 Operator 外部触发（非 finish() 内）。
    void onFinish(FinishCallback cb) { on_finish_ = std::move(cb); }

    // ============================================================
    // 宿主（Viewport）管理接口
    // ============================================================

    /// 由宿主设置状态（push→Active、pop→Inactive）
    void setState(State s) { state_ = s; }
    /// 由宿主读取完成回调（检测到 isFinished 后于 Operator 外部触发）
    const FinishCallback& finishHook() const { return on_finish_; }

    // ============================================================
    // 鼠标事件（子类覆盖）。返回 true=已消费，false=忽略。
    // ============================================================

    virtual bool onMousePress(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }
    virtual bool onMouseRelease(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }
    virtual bool onMouseMove(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }
    virtual bool onMouseDoubleClick(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }
    virtual bool onWheel(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }

    // ============================================================
    // 键盘事件（子类覆盖）
    // ============================================================

    virtual bool onKeyPress(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }
    virtual bool onKeyRelease(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }

    // ============================================================
    // 生命周期
    // ============================================================

    virtual void onActivate(Camera& cam) { (void) cam; }
    virtual void onDeactivate(Camera& cam) { (void) cam; }

    /// 被宿主取消（FocusLost / PointerCancel / 文档切换 / 控件销毁）。
    /// 子类应清理临时交互状态（drag、preview handle 等）。必须幂等。
    /// 默认实现调用 onCancel()，子类通常覆盖 onCancel 而非本方法。
    virtual bool cancel(CancelReason) {
        return onCancel();
    }

    /// 取消钩子：子类覆盖以清理状态。必须幂等（可被多次调用）。
    virtual bool onCancel() { return false; }

    // ============================================================
    // 统一分发入口
    //
    // 调度顺序：
    //   1. PointerCancel / FocusLost → cancel()（生命周期事件优先于空间数据）
    //   2. 若子类覆盖了 handleCameraEvent，且该事件被判定为相机事件，
    //      先交给它处理（模态工具可借此让中键/右键/滚轮始终操控相机）。
    //   3. 未消费则按 type 分发给 onMouse*/onKey* 等具体处理。
    // ============================================================

    virtual bool handleEvent(const InputEvent& e, Camera& cam) {
        if (!isActive())
            return false;

        // 1. 生命周期取消优先
        if (e.isCancelEvent()) {
            return cancel(CancelReason::System);
        }

        // 2. 相机事件拦截钩子（默认实现返回 false，见下）
        if (isCameraEvent(e)) {
            if (bool r = handleCameraEvent(e, cam))
                return r;
        }

        // 3. 常规分发
        switch (e.type) {
        case InputEvent::Type::MousePress: return onMousePress(e, cam);
        case InputEvent::Type::MouseRelease: return onMouseRelease(e, cam);
        case InputEvent::Type::MouseMove: return onMouseMove(e, cam);
        case InputEvent::Type::MouseDoubleClick: return onMouseDoubleClick(e, cam);
        case InputEvent::Type::Wheel: return onWheel(e, cam);
        case InputEvent::Type::KeyPress: return onKeyPress(e, cam);
        case InputEvent::Type::KeyRelease: return onKeyRelease(e, cam);
        case InputEvent::Type::PointerCancel:
        case InputEvent::Type::FocusLost:
            return cancel(CancelReason::System);  // isCancelEvent 已在上方处理，此处兜底
        }
        return false;
    }

protected:
    // ============================================================
    // 子类工具
    // ============================================================

    /// 结束交互。completed=true → Finished；false → Cancelled。
    /// 仅翻转状态，不触发回调。重复调用安全（幂等）。
    void finish(bool completed = true) {
        if (state_ == State::Finished || state_ == State::Cancelled)
            return;
        state_ = completed ? State::Finished : State::Cancelled;
    }

    /// 判定是否为相机操控事件（中键 / 右键 / 滚轮）。
    /// 模态工具可覆盖以自定义相机事件范围。
    /// 注意 button/buttons 语义：press/release 查 button（本次变化），
    /// move 查 buttons（按住集合，move 的 button 恒为 None）。
    virtual bool isCameraEvent(const InputEvent& e) const {
        (void) e;
        return false;  // 默认无相机事件；CameraManipulator 自己处理一切
    }

    /// 相机事件处理器。默认空实现；模态工具通常会组合一个 CameraManipulator
    /// 并在此转发给它。
    virtual bool handleCameraEvent(const InputEvent& e, Camera& cam) {
        (void) e;
        (void) cam;
        return false;
    }

    State state_ = State::Inactive;
    FinishCallback on_finish_;
};

}  // namespace mulan::engine
