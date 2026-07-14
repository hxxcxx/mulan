/**
 * @file tool_controller.cpp
 * @brief ToolController 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "tool_controller.h"

#include <mulan/core/log/log.h>

#include <string>

namespace mulan::editor {
namespace {

const char* finishReasonName(ToolFinishReason reason) {
    switch (reason) {
    case ToolFinishReason::Finished: return "finished";
    case ToolFinishReason::Cancelled: return "cancelled";
    case ToolFinishReason::Replaced: return "replaced";
    }
    return "unknown";
}

}  // namespace

ToolController::~ToolController() = default;

EditorAction ToolController::start(std::unique_ptr<EditorTool> tool) {
    // 替换旧工具时，保留其 end action 的 clearPreview 语义（原先被丢弃）。
    EditorAction replacedEnd = clear(ToolFinishReason::Replaced);
    active_tool_ = std::move(tool);
    if (active_tool_) {
        LOG_DEBUG("[Editor] Tool started: id={}", active_tool_->id());
        EditorAction beginAction = active_tool_->begin();
        mergeEndAction(beginAction, std::move(replacedEnd));
        return beginAction;
    }
    return replacedEnd;
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
        EditorAction endAction = clear(ToolFinishReason::Finished);
        mergeEndAction(action, std::move(endAction));
    } else if (action.lifecycle() == ToolLifecycle::Cancelled) {
        EditorAction endAction = clear(ToolFinishReason::Cancelled);
        mergeEndAction(action, std::move(endAction));
    }

    return action;
}

void ToolController::mergeEndAction(EditorAction& action, EditorAction endAction) {
    // 以工具返回的 action 为主，补入 end action 的 clearPreview（若工具未显式设置）。
    // 这修复了原先 clear(Replaced/Finished/Cancelled) 返回的 end action 被丢弃的问题，
    // 使工具的 end() 清理（如 TransformTool::end 返回 clearPreview）不再丢失。
    if (endAction.shouldClearPreview() && !action.shouldClearPreview() && !action.preview() &&
        !action.hasPreviewReferences()) {
        action.clearPreviewOnApply();
    }
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
    const std::string toolId(tool->id());
    LOG_DEBUG("[Editor] Tool ended: id={}, reason={}", toolId, finishReasonName(reason));
    return tool->end(reason);
}

}  // namespace mulan::editor
