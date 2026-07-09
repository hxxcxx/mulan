#include "document_render_cache.h"

#include "document_session.h"

#include <mulan/io/document.h>
#include <mulan/scene/scene.h>

namespace mulan::app {

void DocumentRenderCache::clear() {
    assets_ = nullptr;
    render_scene_.clear();
}

bool DocumentRenderCache::sync(DocumentSession* session) {
    if (!session || !session->document() || !session->document()->scene() || !session->document()->assets()) {
        clear();
        return false;
    }

    assets_ = session->document()->assets();
    render_scene_.sync(*session->document()->scene(), *assets_);
    return true;
}

const view::RenderScene* DocumentRenderCache::renderScene() const {
    return assets_ ? &render_scene_ : nullptr;
}

std::span<const engine::Light> DocumentRenderCache::lights() const {
    if (!assets_) {
        return {};
    }
    return render_scene_.lights();
}

}  // namespace mulan::app
