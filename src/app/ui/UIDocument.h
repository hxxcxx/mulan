/**
 * @file UIDocument.h
 * @brief UI 文档 — 桥接 Document 数据层与 Viewport 渲染层
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构)
 *
 * 职责：
 *  - 持有 Document（数据源）
 *  - 通过 SceneBuilder 构建旧 Scene（过渡期兼容）
 *  - 绑定 Viewport，设置 World
 *  - 管理拾取映射（pickId → EntityId）
 *
 * 位于 QtApp 层，是唯一同时依赖 Document 模块和 World 模块的类。
 */
#pragma once

#include <mulan/document/Document.h>
#include <mulan/world/World.h>
#include <mulan/engine/math/Math.h>
#include <mulan/engine/math/AABB.h>

#include <memory>
#include <unordered_map>

namespace mulan::engine {
class EngineView;
class Scene;
}

namespace mulan::world {
class Viewport;
}

class UIDocument {
public:
    explicit UIDocument(mulan::document::Document* doc);
    ~UIDocument();

    // --- 数据层 ---

    mulan::document::Document& document() { return *m_doc; }
    const mulan::document::Document& document() const { return *m_doc; }

    // --- 场景（旧路径，过渡期兼容）---
    mulan::engine::Scene* scene() { return m_scene.get(); }

    // --- World（新路径）---
    mulan::world::World* world() { return m_world.get(); }

    // --- 视图连接（新路径）---

    /// 绑定 Viewport：构建 World + 设置 + 适配相机
    void attachViewport(mulan::world::Viewport* viewport);

    /// 解除绑定
    void detachViewport();

    mulan::world::Viewport* viewport() const { return m_viewport; }

    // --- 视图连接（旧路径，过渡期兼容）---
    void attachView(mulan::engine::EngineView* view);
    void detachView();
    mulan::engine::EngineView* view() const { return m_engineView; }

    /// 通过 pickId 反查 EntityId（拾取用）
    mulan::document::EntityId resolvePickId(uint32_t pickId) const;

private:
    mulan::document::Document*  m_doc;   // 不拥有，由 DocumentManager 管理

    // --- 旧路径 ---
    mulan::engine::EngineView*  m_engineView = nullptr;
    std::unique_ptr<mulan::engine::Scene> m_scene;

    // --- 新路径 ---
    mulan::world::Viewport*     m_viewport = nullptr;
    std::unique_ptr<mulan::world::World> m_world;

    /// pickId → EntityId 映射（拾取反查用）
    std::unordered_map<uint32_t, mulan::document::EntityId> m_pickIdMap;
};
