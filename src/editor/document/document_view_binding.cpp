#include "document_view_binding.h"

#include <utility>

namespace mulan::editor {

DocumentViewBinding::DocumentViewBinding() = default;

DocumentViewBinding::~DocumentViewBinding() {
    unbind();
}

void DocumentViewBinding::bind(DocumentSession& session, mulan::view::ViewContext& view) {
    unbind();
    render_binding_.bind(session, view);
    pick_bridge_.bind(render_binding_);
    selection_bridge_.bind(session);
}

void DocumentViewBinding::unbind() {
    selection_bridge_.unbind();
    pick_bridge_.unbind();
    render_binding_.unbind();
}

void DocumentViewBinding::setFrameInvalidationCallback(std::function<void()> callback) {
    render_binding_.setFrameInvalidationCallback(std::move(callback));
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
