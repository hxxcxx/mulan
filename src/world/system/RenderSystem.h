/**
 * @file RenderSystem.h
 * @brief 渲染 System — SceneProxy 管理 + StaticDrawList + DynamicDrawList
 *
 * update() 流程：
 * 1. Destroyed → 移除 SceneProxy → 释放 GPU 资源
 * 2. Created → 创建 SceneProxy → 上传 GPU
 * 3. Geometry/Material/Transform dirty → 更新 SceneProxy → 增量更新 DrawList
 * 4. 产出 MeshDrawCommand 列表 → ForwardPass/EdgePass 消费
 *
 * @author hxxcxx
 * @date 2026-05-29 (原始) / 2026-06-01 (Phase 3 重写)
 */

#pragma once

#include "System.h"
#include "../SceneProxy.h"
#include "../StaticDrawList.h"
#include "../DynamicDrawList.h"
#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/scene/camera/Camera.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mulan::world {

class RenderSystem : public System {
public:
    explicit RenderSystem(engine::GpuResourceManager& gpu, const engine::Camera& camera);

    void update(World& world, float dt) override;

    /// 静态绘制命令（面 + 边线 + 半透明）
    std::span<const MeshDrawCommand> staticFaceCommands() const;
    std::span<const MeshDrawCommand> staticEdgeCommands() const;
    std::span<const MeshDrawCommand> staticTransparentCommands() const;

    /// 动态绘制命令（每帧重建）
    std::span<const MeshDrawCommand> dynamicFaceCommands() const;
    std::span<const MeshDrawCommand> dynamicEdgeCommands() const;

    /// 设置 PSO（由 Viewport 在 initRendering 后调用）
    void setFacePso(engine::PipelineState* pso) { m_facePso = pso; }
    void setEdgePso(engine::PipelineState* pso) { m_edgePso = pso; }
    bool hasFacePso() const { return m_facePso != nullptr; }
    bool hasEdgePso() const { return m_edgePso != nullptr; }

    engine::GpuResourceManager& gpu() { return m_gpu; }

private:
    void processCreated(World& world);
    void processDestroyed(World& world);
    void processDirty(World& world);
    void rebuildStaticList();
    void rebuildDynamicList();

    engine::GpuResourceManager& m_gpu;
    const engine::Camera& m_camera;

    // SceneProxy 管理
    std::unordered_map<Entity::Id, std::unique_ptr<SceneProxy>> m_proxies;
    std::vector<SceneProxy*> m_proxyPtrs;        // 所有 proxy 指针（供遍历）
    std::vector<SceneProxy*> m_dirtyProxies;     // 本帧脏 proxy

    // 绘制列表
    StaticDrawList  m_staticList;
    DynamicDrawList m_dynamicList;

    // PSO（由 Viewport 提供）
    engine::PipelineState* m_facePso = nullptr;
    engine::PipelineState* m_edgePso = nullptr;

    bool m_needsFullRebuild = true;
};

} // namespace mulan::world
