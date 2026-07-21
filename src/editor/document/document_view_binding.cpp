#include "document_view_binding.h"

#include <utility>

namespace mulan::editor {

DocumentViewBinding::DocumentViewBinding(DocumentSession& session, mulan::view::ViewContext& view,
                                         DocumentRenderBinding::FrameInvalidationCallback frameInvalidationCallback)
    : render_binding_(session, view, std::move(frameInvalidationCallback)),
      pick_bridge_(render_binding_),
      selection_bridge_(session) {
}

void DocumentViewBinding::refresh() {
    render_binding_.refresh();
}

void DocumentViewBinding::fitAll() {
    render_binding_.fitAll();
}

void DocumentViewBinding::prepareFrame(ClipUpdateMode mode) {
    render_binding_.prepareFrame(mode);
}

const mulan::view::RenderScene* DocumentViewBinding::renderScene() const {
    return render_binding_.renderScene();
}

std::optional<mulan::view::RenderScene::PickResult> DocumentViewBinding::pickAt(const mulan::engine::Camera& camera,
                                                                                double x, double y) {
    return pick_bridge_.pickAt(camera, x, y);
}

bool DocumentViewBinding::selectSingle(mulan::scene::EntityId entity) {
    return selection_bridge_.selectSingle(entity);
}

bool DocumentViewBinding::clearSelection() {
    return selection_bridge_.clearSelection();
}

}  // namespace mulan::editor
