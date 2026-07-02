#include "document_session.h"

#include <mulan/world/viewport.h>
#include <mulan/world/world.h>

DocumentSession::DocumentSession(std::unique_ptr<mulan::document::Document> doc)
    : document_(std::move(doc))
{
    syncRenderScene();

    if (document_ && document_->world()) {
        document_->world()->forEachEntity([&](mulan::world::Entity* e) {
            pick_id_map_[e->index()] = e->id();
        });
    }
}

DocumentSession::~DocumentSession() {
    detachViewport();
}

mulan::world::World* DocumentSession::world() {
    return document_ ? document_->world() : nullptr;
}

const mulan::world::World* DocumentSession::world() const {
    return document_ ? document_->world() : nullptr;
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

void DocumentSession::attachViewport(mulan::world::Viewport* viewport) {
    if (viewport_) detachViewport();
    viewport_ = viewport;

    mulan::world::World* w = world();
    if (!w) return;

    viewport->setWorld(w);

    const auto& bounds = render_scene_.sceneBounds();
    if (!bounds.isEmpty())
        viewport->camera().fitToBox(bounds);
}

void DocumentSession::detachViewport() {
    if (viewport_) {
        viewport_->setWorld(nullptr);
        viewport_ = nullptr;
    }
}

mulan::world::Entity::Id DocumentSession::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::world::Entity::INVALID_ID;
}
