/**
 * @file drag_from_anchor_tool.cpp
 * @brief DragFromAnchorTool 骨架实现。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "drag_from_anchor_tool.h"

namespace mulan::editor {

EditorAction DragFromAnchorTool::handleInput(const EditorInput& input) {
    // 右键取消。
    if (input.event.isRightPress()) {
        return EditorAction::cancel();
    }

    // 生命周期事件优先于空间数据（修 P7）：release 必须能结束工具，即使 worldPoint 缺失。
    if (input.event.isLeftRelease()) {
        const auto point = input.worldPoint();
        if (point) {
            return commitAtPoint(input, *point);
        }
        // worldPoint 缺失时用回退提交，确保 release 总能完成。
        if (auto fallback = commitFallback(input)) {
            return *fallback;
        }
        return EditorAction::cancel();  // 无可提交：取消而非吞事件
    }

    // press / move 需要世界点（press 可能用于设锚点或二次点击提交）。
    const auto point = input.worldPoint();
    if (!point) {
        return EditorAction::consumeEvent();
    }

    if (input.event.isLeftPress()) {
        return onAnchorPress(input, *point);
    }

    if (input.event.isMouseMove()) {
        return updateDragPreview(input, *point);
    }

    return EditorAction::ignored();
}

}  // namespace mulan::editor
