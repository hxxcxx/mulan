#include "document_session.h"

#include <mulan/view/view_context.h>

DocumentSession::DocumentSession(std::unique_ptr<mulan::io::Document> doc)
    : document_(std::move(doc))
{
    syncRenderScene();

    render_scene_.forEachProxy([&](const mulan::render_scene::SceneProxy& proxy) {
        pick_id_map_[proxy.entity.index()] = proxy.entity;
    });
}

DocumentSession::~DocumentSession() {
    detachViewContext();
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

void DocumentSession::requestRefresh() {
    syncRenderScene();

    // 重新把派生出的 RenderScene 注入视图（指针值不变，内容已更新），
    // 然后渲染一帧。若视图未 attach（如离屏尚未初始化），仅同步、不重绘。
    if (view_context_) {
        if (document_)
            view_context_->setRenderScene(&render_scene_, document_->assets());
        view_context_->renderFrame();
    }
}

void DocumentSession::attachViewContext(mulan::view::ViewContext* runtime) {
    if (view_context_) detachViewContext();
    view_context_ = runtime;

    runtime->setRenderScene(&render_scene_, document_ ? document_->assets() : nullptr);

    const auto& bounds = render_scene_.sceneBounds();
    if (!bounds.isEmpty())
        runtime->camera().fitToBox(bounds);
}

void DocumentSession::detachViewContext() {
    if (view_context_) {
        view_context_->setRenderScene(nullptr, nullptr);
        view_context_ = nullptr;
    }
}

mulan::scene::EntityId DocumentSession::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::scene::EntityId::invalid();
}
