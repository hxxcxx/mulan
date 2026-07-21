#include "document_selection_bridge.h"

#include "document_session.h"

#include <mulan/document/document.h>
#include <mulan/scene/scene.h>

namespace mulan::editor {

bool DocumentSelectionBridge::selectSingle(scene::EntityId entity) {
    if (!session_.document() || !session_.document()->scene()) {
        return false;
    }

    const bool changed = session_.document()->scene()->selectSingle(entity);
    if (changed) {
        session_.publishChange(DocumentChangeKind::VisualState);
    }
    return changed;
}

bool DocumentSelectionBridge::clearSelection() {
    if (!session_.document() || !session_.document()->scene()) {
        return false;
    }

    const bool changed = session_.document()->scene()->clearSelection();
    if (changed) {
        session_.publishChange(DocumentChangeKind::VisualState);
    }
    return changed;
}

}  // namespace mulan::editor
