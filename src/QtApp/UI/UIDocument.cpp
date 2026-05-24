/**
 * @file UIDocument.cpp
 * @brief UIDocument 实现 — 桥接 Document 数据层与 EngineView 渲染层
 * @author hxxcxx
 * @date 2026-04-23
 */
#include "UIDocument.h"
#include "SceneBuilder.h"

#include <mulan/Engine/Render/EngineView.h>

UIDocument::UIDocument(mulan::document::Document* doc)
    : m_doc(doc)
{}

UIDocument::~UIDocument() {
    detachView();
}

void UIDocument::attachView(mulan::engine::EngineView* view) {
    if (m_view) detachView();
    m_view = view;

    // 一次性构建 Scene
    m_scene = SceneBuilder::build(m_doc);
    m_pickIdMap = SceneBuilder::buildPickIdMap(m_doc);

    // 直接设置场景，EngineView 内部处理收集逻辑
    view->setScene(m_scene.get());

    // 适配相机到场景包围盒
    mulan::engine::AABB sceneBounds;
    m_scene->traverse([&](mulan::engine::SceneNode& node) {
        const auto& bounds = node.worldBoundingBox();
        if (!bounds.isEmpty()) {
            sceneBounds.expand(bounds.min);
            sceneBounds.expand(bounds.max);
        }
    });
    if (!sceneBounds.isEmpty()) {
        view->camera().fitToBox(sceneBounds);
    }
}

void UIDocument::detachView() {
    if (m_view) {
        m_view->clearScene();
        m_view = nullptr;
    }
    m_scene.reset();
    m_pickIdMap.clear();
}

mulan::document::EntityId UIDocument::resolvePickId(uint32_t pickId) const {
    auto it = m_pickIdMap.find(pickId);
    if (it != m_pickIdMap.end()) return it->second;
    return {};
}
