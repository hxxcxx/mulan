/**
 * @file InteractiveOperator.h
 * @brief 模态交互操作器 — 支持模态事件循环的 Operator
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 设计思路：
 *  - 继承 Operator，增加模态交互生命周期
 *  - 通过 exec() 进入模态循环（阻塞），通过 finish() 退出
 *  - 典型流程：exec() → enter() → [事件处理] → finish() → leave()
 *  - 不依赖 Qt，模态阻塞由 InteractionHost 实现
 *
 * 使用示例：
 *   class PickPointOperator : public InteractiveOperator {
 *       void enter(Camera& cam) override { /* 设置提示 * / }
 *       bool onMousePress(const InputEvent& e, Camera& cam) override {
 *           m_point = screenToWorld(e.x, e.y, cam);
 *           finish(InteractionStatus::Normal);  // 结束模态
 *           return true;
 *       }
 *   };
 *
 *   PickPointOperator picker;
 *   auto status = picker.exec(host, camera);  // 阻塞
 *   if (status == InteractionStatus::Normal) use(picker.point());
 */
#pragma once

#include "Operator.h"
#include "InteractionHost.h"
#include "CameraManipulator.h"

namespace mulan::engine {

class InteractiveOperator : public Operator {
public:
    InteractiveOperator() = default;
    ~InteractiveOperator() override = default;

    /// 启动模态交互（阻塞直到 finish() 被调用）
    InteractionStatus exec(InteractionHost& host, Camera& cam) {
        m_host = &host;
        enter(cam);
        m_status = host.run(*this, cam);
        leave(cam, m_status);
        m_host = nullptr;
        return m_status;
    }

    /// 结束当前模态交互（在事件处理中调用）
    void finish(InteractionStatus status) {
        if (m_host)
            m_host->abort();
        m_status = status;
    }

    /// 获取上一次交互的完成状态
    InteractionStatus status() const { return m_status; }

    /// 覆盖 handleEvent：相机事件优先拦截，其余转发给子类交互逻辑
    bool handleEvent(const InputEvent& e, Camera& cam) override {
        // 中键/右键/滚轮 → 相机操控（交互期间始终可用）
        if (isCameraEvent(e)) {
            if (m_cameraManip.handleEvent(e, cam))
                return true;
        }
        // 其余事件 → 子类的交互处理
        return Operator::handleEvent(e, cam);
    }

protected:
    /// 进入模态时的初始化（设置提示、光标等）
    virtual void enter(Camera& cam) { (void)cam; }

    /// 离开模态时的清理（恢复状态等）
    virtual void leave(Camera& cam, InteractionStatus status) {
        (void)cam; (void)status;
    }

    /// 子类可覆盖以自定义哪些事件算相机事件
    virtual bool isCameraEvent(const InputEvent& e) const {
        if (e.type == InputEvent::Type::Wheel) return true;
        if (e.button == MouseButton::Middle) return true;
        return false;
    }

private:
    InteractionHost* m_host = nullptr;
    InteractionStatus m_status = InteractionStatus::Cancel;
    CameraManipulator m_cameraManip;
};

} // namespace mulan::engine
