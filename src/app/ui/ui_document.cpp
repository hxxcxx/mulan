#include "ui_document.h"

#include <mulan/world/viewport.h>
#include <mulan/world/world.h>

UIDocument::UIDocument(std::unique_ptr<mulan::document::Document> doc)
    : document_(std::move(doc))
{
    // 构建 pickId 映射（pickId = entity.index()）
    if (document_ && document_->world()) {
        document_->world()->forEachEntity([&](mulan::world::Entity* e) {
            pick_id_map_[e->index()] = e->id();
        });
    }
}

UIDocument::~UIDocument() {
    detachViewport();
}

mulan::world::World* UIDocument::world() {
    return document_ ? document_->world() : nullptr;
}

const mulan::world::World* UIDocument::world() const {
    return document_ ? document_->world() : nullptr;
}

const std::string& UIDocument::displayName() const {
    static const std::string empty;
    return document_ ? document_->displayName() : empty;
}

// ============================================================
// 视图连接
// ============================================================

void UIDocument::attachViewport(mulan::world::Viewport* viewport) {
    if (viewport_) detachViewport();
    viewport_ = viewport;

    mulan::world::World* w = world();
    if (!w) return;

    // 设置 World 到 Viewport
    viewport->setWorld(w);

    // 适配相机到场景包围盒
    mulan::engine::AABB sceneBounds;
    w->forEachEntity([&](mulan::world::Entity* e) {
        if (e->geometry()) {
            auto bounds = e->geometry()->bounds();
            auto worldBox = bounds.transformed(e->worldTransform());
            sceneBounds.expand(worldBox.min);
            sceneBounds.expand(worldBox.max);
        }
    });
    if (!sceneBounds.isEmpty()) {
        viewport->camera().fitToBox(sceneBounds);
    }
}

void UIDocument::detachViewport() {
    if (viewport_) {
        viewport_->setWorld(nullptr);
        viewport_ = nullptr;
    }
}

mulan::world::Entity::Id UIDocument::resolvePickId(uint32_t pickId) const {
    auto it = pick_id_map_.find(pickId);
    if (it != pick_id_map_.end()) return it->second;
    return mulan::world::Entity::INVALID_ID;
}
