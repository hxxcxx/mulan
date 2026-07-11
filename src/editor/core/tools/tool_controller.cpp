/**
 * @file tool_controller.cpp
 * @brief ToolController 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "core/tools/tool_controller.h"

namespace mulan::app {

ToolController::~ToolController() = default;

EditorAction ToolController::start(std::unique_ptr<EditorTool> tool) {
    clear(ToolFinishReason::Replaced);
    active_tool_ = std::move(tool);
    if (active_tool_) {
        return active_tool_->begin();
    }
    return EditorAction::ignored();
}

EditorAction ToolController::handleInput(const EditorInput& input) {
    if (!active_tool_) {
        return EditorAction::ignored();
    }

    if (input.event.type == engine::InputEvent::Type::KeyPress && input.event.key == engine::Key::Escape) {
        return cancel();
    }

    EditorAction action = active_tool_->handleInput(input);
    if (action.lifecycle() == ToolLifecycle::Finished) {
        clear(ToolFinishReason::Finished);
    } else if (action.lifecycle() == ToolLifecycle::Cancelled) {
        clear(ToolFinishReason::Cancelled);
    }

    return action;
}

EditorAction ToolController::cancel() {
    EditorAction action = clear(ToolFinishReason::Cancelled);
    action.consume().clearPreviewOnApply().cancelTool();
    return action;
}

EditorAction ToolController::clear(ToolFinishReason reason) {
    if (!active_tool_) {
        return EditorAction::ignored();
    }

    auto tool = std::move(active_tool_);
    return tool->end(reason);
}

}  // namespace mulan::app
