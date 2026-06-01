/**
 * @file UIDocument.cpp
 * @brief UIDocument 实现 — 桥接 World 数据层与渲染层
 * @author hxxcxx
 * @date 2026-04-23 (原始) / 2026-06-01 (重构)
 */
#include "UIDocument.h"

#include <mulan/world/Viewport.h>

UIDocument::UIDocument(std::unique_ptr<mulan::world::World> world,
                       std::string displayName)
    : m_world(std::move(world))
    , m_displayName(std::move(displayName))
{
    // 构建 pickId 映射（pickId = entity.index()）
    if (m_world) {
        m_world->forEachEntity([&](mulan::world::Entity* e) {
            m_pickIdMap[e->index()] = e->id();
        });
    }
}

UIDocument::~UIDocument() {
    detachViewport();
}

// ============================================================
// 视图连接
// ============================================================

void UIDocument::attachViewport(mulan::world::Viewport* viewport) {
    if (m_viewport) detachViewport();
    m_viewport = viewport;

    // 设置 World 到 Viewport
    viewport->setWorld(m_world.get());

    // 适配相机到场景包围盒
    mulan::engine::AABB sceneBounds;
    m_world->forEachEntity([&](mulan::world::Entity* e) {
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
