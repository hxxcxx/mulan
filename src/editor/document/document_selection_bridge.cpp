#include "document_selection_bridge.h"

#include "document_render_binding.h"
#include "document_session.h"

#include <mulan/io/document.h>
#include <mulan/scene/scene.h>

namespace mulan::editor {

void DocumentSelectionBridge::bind(DocumentSession& session, DocumentRenderBinding& renderBinding) {
    session_ = &session;
    render_binding_ = &renderBinding;
}

void DocumentSelectionBridge::unbind() {
    session_ = nullptr;
    render_binding_ = nullptr;
}

bool DocumentSelectionBridge::selectSingle(scene::EntityId entity) {
    if (!isBound() || !session_->document() || !session_->document()->scene()) {
        return false;
    }

    const bool changed = session_->document()->scene()->selectSingle(entity);
    if (changed) {
        render_binding_->refresh();
    }
    return changed;
}

bool DocumentSelectionBridge::clearSelection() {
    if (!isBound() || !session_->document() || !session_->document()->scene()) {
        return false;
    }

    const bool changed = session_->document()->scene()->clearSelection();
    if (changed) {
        render_binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::editor
