/**
 * @file editor_tool_operator.cpp
 * @brief EditorToolOperator 适配器实现。
 * @author hxxcxx
 * @date 2026-07-14
 */

#include "editor_tool_operator.h"

#include "../session/editor_session.h"

namespace mulan::editor {

EditorToolOperator::EditorToolOperator(EditorSession& session) : session_(session) {}

bool EditorToolOperator::handleEvent(const engine::InputEvent& e, engine::Camera& cam) {
    if (!isActive()) {
        return false;
    }

    // 1. 生命周期取消优先（修 P7：cancel 不依赖 worldPoint）
    if (e.isCancelEvent()) {
        return onCancel();
    }

    // 2. 相机穿透：中键/右键/滚轮先尝试给相机
    if (isCameraEvent(e)) {
        if (bool r = handleCameraEvent(e, cam))
            return r;
    }

    // 3. 驱动活动工具（makeEditorInput → snap → tool dispatch → applyAction）
    const bool consumed = session_.driveActiveTool(e);

    // 4. 工具结束 → Operator finish，外部（ViewContext）将 pop
    if (!session_.hasActiveTool()) {
        finish(true);
    }

    return consumed;
}

bool EditorToolOperator::isCameraEvent(const engine::InputEvent& e) const {
    using T = engine::InputEvent::Type;
    if (e.type == T::Wheel) {
        return true;
    }
    if (e.type == T::MousePress || e.type == T::MouseRelease) {
        // press/release 查 button（本次变化的按钮）
        return e.button & (engine::MouseButton::Middle | engine::MouseButton::Right);
    }
    if (e.type == T::MouseMove) {
        // move 查 buttons（当前按住的集合），因为 move 的 button 恒为 None
        return e.buttons & (engine::MouseButton::Middle | engine::MouseButton::Right);
    }
    return false;
}

bool EditorToolOperator::handleCameraEvent(const engine::InputEvent& e, engine::Camera& cam) {
    return camera_delegate_.handleEvent(e, cam);
}

bool EditorToolOperator::onCancel() {
    session_.cancelActiveTool();
    finish(false);
    return true;
}

}  // namespace mulan::editor
