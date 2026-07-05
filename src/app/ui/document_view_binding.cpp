#include "document_view_binding.h"

#include "document_session.h"

#include <mulan/io/document.h>
#include <mulan/view/render_scene.h>
#include <mulan/view/view_context.h>

#include <unordered_map>

struct DocumentViewBinding::RenderCache {
    mulan::render_scene::RenderScene renderScene;
    std::unordered_map<uint32_t, mulan::scene::EntityId> pickIds;
};

DocumentViewBinding::~DocumentViewBinding() {
    unbind();
}

void DocumentViewBinding::bind(DocumentSession& session, mulan::view::ViewContext& view) {
    unbind();
    session_ = &session;
    view_ = &view;

    syncRenderCache();
    injectRenderCache();
    applyViewPreferences();
}

void DocumentViewBinding::unbind() {
    if (view_) {
        view_->setRenderScene(nullptr, nullptr);
    }
    session_ = nullptr;
    view_ = nullptr;
    render_cache_.reset();
}

void DocumentViewBinding::refresh() {
    if (!isBound()) {
        return;
    }
    syncRenderCache();
    injectRenderCache();
    view_->renderFrame();
}

void DocumentViewBinding::fitAll() {
    if (!isBound()) {
        return;
    }
    const auto& bounds = render_cache_->renderScene.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
    view_->renderFrame();
}

mulan::scene::EntityId DocumentViewBinding::resolvePickId(uint32_t pickId) const {
    if (!render_cache_) {
        return mulan::scene::EntityId::invalid();
    }
    auto it = render_cache_->pickIds.find(pickId);
    if (it != render_cache_->pickIds.end()) {
        return it->second;
    }
    return mulan::scene::EntityId::invalid();
}

void DocumentViewBinding::syncRenderCache() {
    if (!render_cache_) {
        render_cache_ = std::make_unique<RenderCache>();
    }

    if (!session_ || !session_->document() ||
        !session_->document()->scene() || !session_->document()->assets()) {
        render_cache_->renderScene.clear();
        render_cache_->pickIds.clear();
        return;
    }

    render_cache_->renderScene.sync(*session_->document()->scene(), *session_->document()->assets());
    rebuildPickIdMap();
}

void DocumentViewBinding::rebuildPickIdMap() {
    if (!render_cache_) {
        return;
    }
    render_cache_->pickIds.clear();
    render_cache_->renderScene.forEachProxy([&](const mulan::render_scene::SceneProxy& proxy) {
        render_cache_->pickIds[proxy.entity.index()] = proxy.entity;
    });
}

void DocumentViewBinding::applyViewPreferences() {
    if (!isBound()) {
        return;
    }

    const auto& preferences = session_->renderPreferences();
    view_->camera().setOrthographic(preferences.preferOrthographic);
    if (preferences.preferIBL) {
        view_->enableIBL();
    }

    const auto& bounds = render_cache_->renderScene.sceneBounds();
    if (!bounds.isEmpty()) {
        view_->camera().fitToBox(bounds);
    }
}

void DocumentViewBinding::injectRenderCache() {
    if (!isBound()) {
        return;
    }
    auto* document = session_->document();
    view_->setRenderScene(render_cache_ ? &render_cache_->renderScene : nullptr,
                          document ? document->assets() : nullptr);
}
