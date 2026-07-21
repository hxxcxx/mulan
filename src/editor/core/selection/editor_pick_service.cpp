#include "editor_pick_service.h"

#include "editor_render_pick_conversion.h"
#include "document/document_session.h"
#include "document/document_view_binding.h"

#include <mulan/document/document.h>
#include <mulan/view/core/view_context.h>

namespace mulan::editor {

bool EditorPickService::isReady() const {
    return view_.isReady();
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
    input.pickWorld.bind(binding_.renderScene());
    if (!hasCursorPosition(event)) {
        return input;
    }

    input.tested = true;
    if (const auto pick = binding_.pickAt(view_.camera(), static_cast<double>(event.x), static_cast<double>(event.y))) {
        input.hit = editorPickHitFromRenderPick(*pick);
    }
    input.pickWorld.bind(binding_.renderScene());
    return input;
}

std::optional<EditorPickHit> EditorPickService::pickAtFramebuffer(double screenX, double screenY) const {
    const auto pick = binding_.pickAt(view_.camera(), screenX, screenY);
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
    if (!isReady() || !session_.document()) {
        return std::nullopt;
    }

    const std::optional<EditorPickHit> pick = pickAtFramebuffer(screenX, screenY);
    if (!pick) {
        return std::nullopt;
    }
    return makeEditorSelectionHit(*pick, *session_.document());
}

}  // namespace mulan::editor
