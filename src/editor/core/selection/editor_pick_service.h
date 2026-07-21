/**
 * @file editor_pick_service.h
 * @brief EditorPickService 将视图拾取结果转换为编辑器拾取与选择上下文。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "editor_input.h"
#include "editor_selection.h"

#include <mulan/interaction/input_event.h>

#include <optional>

namespace mulan::view {
class ViewContext;
}  // namespace mulan::view

namespace mulan::editor {

class DocumentSession;
class DocumentViewBinding;

struct EditorPickInput {
    bool tested = false;
    std::optional<EditorPickHit> hit;
    EditorPickQueryWorld pickWorld;
};

class EditorPickService {
public:
    EditorPickService(DocumentSession& session, view::ViewContext& view, DocumentViewBinding& binding)
        : session_(session), view_(view), binding_(binding) {}

    bool isReady() const;
    static bool hasCursorPosition(const engine::InputEvent& event);

    EditorPickInput inputPick(const engine::InputEvent& event) const;
    std::optional<EditorPickHit> pickAtFramebuffer(double screenX, double screenY) const;
    std::optional<EditorSelectionHit> selectionHitAtFramebuffer(double screenX, double screenY) const;

private:
    DocumentSession& session_;
    view::ViewContext& view_;
    DocumentViewBinding& binding_;
};

}  // namespace mulan::editor
