/**
 * @file tool_controller.cpp
 * @brief ToolController 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "tool_controller.h"

namespace mulan::app {

ToolController::~ToolController() = default;

void ToolController::start(std::unique_ptr<EditorTool> tool, ToolContext& context) {
    clear(context, ToolFinishReason::Replaced);
    active_tool_ = std::move(tool);
    if (active_tool_) {
        active_tool_->begin(context);
    }
}

bool ToolController::handleInput(ToolContext& context, const EditorInput& input) {
    if (!active_tool_) {
        return false;
    }

    if (input.event.type == engine::InputEvent::Type::KeyPress && input.event.key == engine::Key::Escape) {
        cancel(context);
        return true;
    }

    const ToolInputResult result = active_tool_->handleInput(context, input);
    switch (result) {
    case ToolInputResult::Ignored: return false;
    case ToolInputResult::Consumed: return true;
    case ToolInputResult::Finished: clear(context, ToolFinishReason::Finished); return true;
    case ToolInputResult::Cancelled: clear(context, ToolFinishReason::Cancelled); return true;
    }

    return true;
}

void ToolController::cancel(ToolContext& context) {
    clear(context, ToolFinishReason::Cancelled);
}

void ToolController::clear(ToolContext& context, ToolFinishReason reason) {
    if (!active_tool_) {
        return;
    }

    auto tool = std::move(active_tool_);
    tool->end(context, reason);
}

}  // namespace mulan::app
