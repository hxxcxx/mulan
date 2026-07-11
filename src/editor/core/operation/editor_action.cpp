/**
 * @file editor_action.cpp
 * @brief EditorAction 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "core/operation/editor_action.h"

#include <utility>

namespace mulan::app {

EditorAction EditorAction::ignored() {
    return EditorAction{};
}

EditorAction EditorAction::consumeEvent() {
    EditorAction action;
    action.consumed_ = true;
    return action;
}

EditorAction EditorAction::setPreview(DraftGeometry geometry) {
    EditorAction action = consumeEvent();
    action.preview_ = std::move(geometry);
    return action;
}

EditorAction EditorAction::setPreviewReferences(std::vector<view::PreviewReference> references) {
    EditorAction action = consumeEvent();
    action.preview_references_ = std::move(references);
    return action;
}

EditorAction EditorAction::clearPreview() {
    EditorAction action = consumeEvent();
    action.clear_preview_ = true;
    return action;
}

EditorAction EditorAction::commit(DocumentOperation operation) {
    EditorAction action = consumeEvent();
    action.operation_ = std::move(operation);
    return action;
}

EditorAction EditorAction::finish() {
    EditorAction action = consumeEvent();
    action.lifecycle_ = ToolLifecycle::Finished;
    return action;
}

EditorAction EditorAction::cancel() {
    EditorAction action = consumeEvent();
    action.clear_preview_ = true;
    action.lifecycle_ = ToolLifecycle::Cancelled;
    return action;
}

EditorAction& EditorAction::consume() {
    consumed_ = true;
    return *this;
}

EditorAction& EditorAction::clearPreviewOnApply() {
    clear_preview_ = true;
    return *this;
}

EditorAction& EditorAction::finishTool() {
    lifecycle_ = ToolLifecycle::Finished;
    return *this;
}

EditorAction& EditorAction::cancelTool() {
    lifecycle_ = ToolLifecycle::Cancelled;
    return *this;
}

}  // namespace mulan::app
