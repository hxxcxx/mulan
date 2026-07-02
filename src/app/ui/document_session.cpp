#include "document_session.h"

#include <mulan/view/viewport.h>

DocumentSession::DocumentSession(std::unique_ptr<mulan::document::Document> doc)
    : document_(std::move(doc))
{
    syncRenderScene();

    render_scene_.forEachProxy([&](const mulan::render_scene::SceneProxy& proxy) {
        pick_id_map_[proxy.entity.index()] = proxy.entity;
    });
}

DocumentSession::~DocumentSession() {
    detachViewport();
}

const std::string& DocumentSession::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}

void DocumentSession::syncRenderScene() {
    if (!document_ || !document_->scene() || !document_->assets()) {
        render_scene_.clear();
        return;
    }

    render_scene_.sync(*document_->scene(), *document_->assets());
}

void DocumentSession::attachViewport(mulan::view::Viewport* viewport) {
    if (viewport_) detachViewport();
    viewport_ = viewport;

    viewport->setRenderScene(&render_scene_, document_ ? document_->assets() : nullptr);

    const auto& bounds = render_scene_.sceneBounds();
    if (!bounds.isEmpty())
        viewport->camera().fitToBox(bounds);
}

void DocumentSession::detachViewport() {
    if (viewport_) {
        viewport_->setRenderScene(nullptr, nullptr);
        viewport_ = nullptr;
    }
}

mulan::scene::EntityId DocumentSession::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::scene::EntityId::invalid();
}
