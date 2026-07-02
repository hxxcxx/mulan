#include "document_session.h"

#include <mulan/view/view_runtime.h>

DocumentSession::DocumentSession(std::unique_ptr<mulan::document::Document> doc)
    : document_(std::move(doc))
{
    syncRenderScene();

    render_scene_.forEachProxy([&](const mulan::render_scene::SceneProxy& proxy) {
        pick_id_map_[proxy.entity.index()] = proxy.entity;
    });
}

DocumentSession::~DocumentSession() {
    detachViewRuntime();
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

void DocumentSession::attachViewRuntime(mulan::view::ViewRuntime* runtime) {
    if (view_runtime_) detachViewRuntime();
    view_runtime_ = runtime;

    runtime->setRenderScene(&render_scene_, document_ ? document_->assets() : nullptr);

    const auto& bounds = render_scene_.sceneBounds();
    if (!bounds.isEmpty())
        runtime->camera().fitToBox(bounds);
}

void DocumentSession::detachViewRuntime() {
    if (view_runtime_) {
        view_runtime_->setRenderScene(nullptr, nullptr);
        view_runtime_ = nullptr;
    }
}

mulan::scene::EntityId DocumentSession::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::scene::EntityId::invalid();
}
