/**
 * @file UIDocument.cpp
 * @brief UIDocument 实现 — 桥接 Document 数据层与渲染层
 * @author hxxcxx
 * @date 2026-04-23 (原始) / 2026-06-01 (重构)
 */
#include "UIDocument.h"
#include "SceneBuilder.h"

#include <mulan/world/Viewport.h>
#include <mulan/world/World.h>
#include <mulan/world/system/System.h>

// 注意：不再 include EngineView.h，因为其内部 pass/RenderPass.h 与
// Viewport 引入的 graph/RenderPass.h 存在类名冲突（PassContext/RenderPass）。
// 旧路径 attachView 保留为空壳，过渡期不使用。

UIDocument::UIDocument(mulan::document::Document* doc)
    : m_doc(doc)
{}

UIDocument::~UIDocument() {
    detachViewport();
}

// ============================================================
// 新路径：Viewport + World
// ============================================================

void UIDocument::attachViewport(mulan::world::Viewport* viewport) {
    if (m_viewport) detachViewport();
    m_viewport = viewport;

    // 从 Document 构建 World（过渡期：仍走 SceneBuilder 构建 Scene，
    // Phase 2 将由 Importer 直接填充 World）
    m_world = std::make_unique<mulan::world::World>();

    // 构建 pickId 映射
    m_pickIdMap = SceneBuilder::buildPickIdMap(m_doc);

    // 设置 World 到 Viewport
    viewport->setWorld(m_world.get());

    // 适配相机到场景包围盒（从 Document 的 Entity 几何计算）
    mulan::engine::AABB sceneBounds;
    m_doc->forEachEntity([&](const mulan::document::Entity& e) {
        if (e.geometry()) {
            auto bounds = e.geometry()->boundingBox();
            auto worldBox = bounds.transformed(e.localTransform());
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
    m_world.reset();
}

// ============================================================
// 旧路径：EngineView + Scene（过渡期空壳，已弃用）
// ============================================================

void UIDocument::attachView(mulan::engine::EngineView* view) {
    m_engineView = view;
    // 旧路径已弃用，不做任何操作
}

void UIDocument::detachView() {
    m_engineView = nullptr;
    m_scene.reset();
}

mulan::document::EntityId UIDocument::resolvePickId(uint32_t pickId) const {
    auto it = m_pickIdMap.find(pickId);
    if (it != m_pickIdMap.end()) return it->second;
    return {};
}
