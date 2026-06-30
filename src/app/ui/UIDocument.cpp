/**
 * @file UIDocument.cpp
 * @brief UIDocument 实现 — Document 的视图层包装
 * @author hxxcxx
 * @date 2026-04-23 (原始) / 2026-06-30 (持有 Document)
 */
#include "UIDocument.h"

#include <mulan/world/Viewport.h>
#include <mulan/world/World.h>

UIDocument::UIDocument(std::unique_ptr<mulan::document::Document> doc)
    : m_document(std::move(doc))
{
    // 构建 pickId 映射（pickId = entity.index()）
    if (m_document && m_document->world()) {
        m_document->world()->forEachEntity([&](mulan::world::Entity* e) {
            m_pickIdMap[e->index()] = e->id();
        });
    }
}

UIDocument::~UIDocument() {
    detachViewport();
}

mulan::world::World* UIDocument::world() {
    return m_document ? m_document->world() : nullptr;
}

const mulan::world::World* UIDocument::world() const {
    return m_document ? m_document->world() : nullptr;
}

const std::string& UIDocument::displayName() const {
    static const std::string empty;
    return m_document ? m_document->displayName() : empty;
}

// ============================================================
// 视图连接
// ============================================================

void UIDocument::attachViewport(mulan::world::Viewport* viewport) {
    if (m_viewport) detachViewport();
    m_viewport = viewport;

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
    if (m_viewport) {
        m_viewport->setWorld(nullptr);
        m_viewport = nullptr;
    }
}

mulan::world::Entity::Id UIDocument::resolvePickId(uint32_t pickId) const {
    auto it = m_pickIdMap.find(pickId);
    if (it != m_pickIdMap.end()) return it->second;
    return mulan::world::Entity::INVALID_ID;
}
