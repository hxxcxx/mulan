/**
 * @file editor_tool_operator.cpp
 * @brief EditorToolOperator 适配器实现。
 * @author hxxcxx
 * @date 2026-07-14
 */

#include "editor_tool_operator.h"

#include "../session/editor_session.h"

namespace mulan::editor {

EditorToolOperator::EditorToolOperator(EditorSession& session) : session_(session) {
    // 活动工具期间左键与右键都属于工具；相机委托只接受中键平移和滚轮缩放。
    camera_delegate_.config.orbitButton = engine::MouseButton::None;
    camera_delegate_.config.panAltButton = engine::MouseButton::None;
}

bool EditorToolOperator::handleEvent(const engine::InputEvent& e, engine::Camera& cam) {
    return dispatchEvent(e, cam).handled();
}

engine::InputOutcome EditorToolOperator::dispatchEvent(const engine::InputEvent& e, engine::Camera& cam) {
    if (!isActive()) {
        return engine::InputOutcome::ignored();
    }

    // 1. 生命周期取消优先；onCancel 只标记完成，由 ViewContext 在本调用返回后弹栈。
    if (e.isCancelEvent()) {
        cancel(engine::CancelReason::System);
        return engine::InputOutcome::handledBy(engine::InputDisposition::Cancelled);
    }

    // 2. 相机穿透：中键/滚轮先尝试给相机。右键不会进入此分支，明确交给工具。
    if (isCameraEvent(e)) {
        const engine::InputOutcome cameraOutcome = camera_delegate_.dispatchEvent(e, cam);
        if (cameraOutcome.handled()) {
            return cameraOutcome;
        }
    }

    // 3. 驱动活动工具（makeEditorInput → snap → tool dispatch → applyAction）
    const EditorToolDispatchResult result = session_.driveActiveTool(e);

    // 4. 工具结束 → Operator finish，外部（ViewContext）将 pop。
    // 必须使用 ToolLifecycle 区分正常完成和取消，不能仅凭 activeTool==nullptr 猜测。
    if (!result.hadActiveTool) {
        finish(false);
        return engine::InputOutcome::handledBy(engine::InputDisposition::ModalInteraction);
    }
    if (result.lifecycle != ToolLifecycle::Running) {
        finish(result.lifecycle == ToolLifecycle::Finished);
    }

    // 模态工具拥有本次事件，即使具体工具返回 ignored，也不能让 release 落回选择逻辑。
    return engine::InputOutcome::handledBy(engine::InputDisposition::ModalInteraction);
}

void EditorToolOperator::onActivate(engine::Camera& cam) {
    camera_delegate_.setState(engine::Operator::State::Active);
    camera_delegate_.onActivate(cam);
}

void EditorToolOperator::onDeactivate(engine::Camera& cam) {
    camera_delegate_.cancel(engine::CancelReason::System);
    camera_delegate_.onDeactivate(cam);
    camera_delegate_.setState(engine::Operator::State::Inactive);
}

bool EditorToolOperator::isCameraEvent(const engine::InputEvent& e) const {
    using T = engine::InputEvent::Type;
    if (e.type == T::Wheel) {
        return true;
    }
    if (e.type == T::MousePress || e.type == T::MouseRelease) {
        // press/release 查 button（本次变化的按钮）
        return e.button == engine::MouseButton::Middle;
    }
    if (e.type == T::MouseMove) {
        // move 查 buttons（当前按住的集合），因为 move 的 button 恒为 None
        return e.buttons & engine::MouseButton::Middle;
    }
    return false;
}

bool EditorToolOperator::handleCameraEvent(const engine::InputEvent& e, engine::Camera& cam) {
    return camera_delegate_.handleEvent(e, cam);
}

bool EditorToolOperator::onCancel() {
    camera_delegate_.cancel(engine::CancelReason::System);
    session_.cancelActiveToolFromOperator();
    finish(false);
    return true;
}

}  // namespace mulan::editor
