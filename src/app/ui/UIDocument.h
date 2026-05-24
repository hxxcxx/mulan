/**
 * @file UIDocument.h
 * @brief UI 文档 — 桥接 Document 数据层与 EngineView 渲染层
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 职责：
 *  - 持有 Document（数据）
 *  - 通过 SceneBuilder 一次性构建 Scene
 *  - collector 回调从 Scene 遍历可见节点填充 RenderQueue
 *  - 管理视图绑定（attach/detach）
 *
 * 位于 QtApp 层，是唯一同时依赖 Document 模块和 Engine 模块的类。
 */
#pragma once

#include <mulan/document/Document.h>
#include <mulan/engine/scene/Scene.h>
#include <mulan/engine/render/RenderGeometry.h>
#include <mulan/engine/scene/camera/Camera.h>
#include <mulan/engine/math/Math.h>
#include <mulan/engine/math/AABB.h>

#include <memory>
#include <unordered_map>

namespace mulan::engine {
class EngineView;
}

class UIDocument {
public:
    explicit UIDocument(mulan::document::Document* doc);
    ~UIDocument();

    // --- 数据层 ---

    mulan::document::Document& document() { return *m_doc; }
    const mulan::document::Document& document() const { return *m_doc; }

    // --- 场景 ---

    mulan::engine::Scene* scene() { return m_scene.get(); }
    const mulan::engine::Scene* scene() const { return m_scene.get(); }

    // --- 视图连接 ---

    /// 绑定 EngineView（构建场景 + 设置 collector 回调 + 适配相机）
    void attachView(mulan::engine::EngineView* view);

    /// 解除绑定
    void detachView();

    mulan::engine::EngineView* view() const { return m_view; }

    /// 通过 pickId 反查 EntityId（拾取用）
    mulan::document::EntityId resolvePickId(uint32_t pickId) const;

private:
    mulan::document::Document*  m_doc;   // 不拥有，由 DocumentManager 管理
    mulan::engine::EngineView*  m_view = nullptr;

    /// 由 SceneBuilder 一次性构建的渲染场景
    std::unique_ptr<mulan::engine::Scene> m_scene;

    /// pickId → EntityId 映射（拾取反查用）
    std::unordered_map<uint32_t, mulan::document::EntityId> m_pickIdMap;
};
