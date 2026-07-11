#include "editor_pick_service.h"

#include "editor_render_pick_conversion.h"
#include "document/document_session.h"
#include "document/document_view_binding.h"

#include <mulan/io/document.h>
#include <mulan/view/core/view_context.h>

namespace mulan::editor {

void EditorPickService::bind(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding) {
    session_ = session;
    view_ = view;
    binding_ = binding;
}

void EditorPickService::unbind() {
    session_ = nullptr;
    view_ = nullptr;
    binding_ = nullptr;
}

bool EditorPickService::isBound() const {
    return session_ != nullptr && view_ != nullptr && view_->isInitialized() && binding_ != nullptr;
}

bool EditorPickService::hasCursorPosition(const engine::InputEvent& event) {
    switch (event.type) {
    case engine::InputEvent::Type::MousePress:
    case engine::InputEvent::Type::MouseRelease:
    case engine::InputEvent::Type::MouseMove:
    case engine::InputEvent::Type::MouseDoubleClick:
    case engine::InputEvent::Type::Wheel: return true;
    case engine::InputEvent::Type::KeyPress:
    case engine::InputEvent::Type::KeyRelease: return false;
    }
    return false;
}

EditorPickInput EditorPickService::inputPick(const engine::InputEvent& event) const {
    EditorPickInput input;
    if (!binding_) {
        return input;
    }

    input.pickWorld.bind(binding_->renderScene());
    if (!view_ || !hasCursorPosition(event)) {
        return input;
    }

    input.tested = true;
    if (const auto pick =
                binding_->pickAt(view_->camera(), static_cast<double>(event.x), static_cast<double>(event.y))) {
        input.hit = editorPickHitFromRenderPick(*pick);
    }
    input.pickWorld.bind(binding_->renderScene());
    return input;
}

std::optional<EditorPickHit> EditorPickService::pickAtFramebuffer(double screenX, double screenY) const {
    if (!binding_ || !view_) {
        return std::nullopt;
    }

    const auto pick = binding_->pickAt(view_->camera(), screenX, screenY);
    if (!pick) {
        return std::nullopt;
    }

    EditorPickHit editorPick = editorPickHitFromRenderPick(*pick);
    if (!editorPick.valid()) {
        return std::nullopt;
    }
    return editorPick;
}

std::optional<EditorSelectionHit> EditorPickService::selectionHitAtFramebuffer(double screenX, double screenY) const {
    if (!isBound() || !session_ || !session_->document()) {
        return std::nullopt;
    }

    const std::optional<EditorPickHit> pick = pickAtFramebuffer(screenX, screenY);
    if (!pick) {
        return std::nullopt;
    }
    return makeEditorSelectionHit(*pick, *session_->document());
}

}  // namespace mulan::editor
