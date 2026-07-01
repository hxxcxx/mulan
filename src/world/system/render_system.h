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

#include "system.h"
#include "../scene_proxy.h"
#include "../static_draw_list.h"
#include "../dynamic_draw_list.h"
#include "mulan/engine/render/gpu_resource_manager.h"
#include "mulan/engine/scene/camera/camera.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mulan::engine {
class MaterialCache;
}

namespace mulan::world {

class RenderSystem : public System {
public:
    explicit RenderSystem(engine::GpuResourceManager& gpu,
                          engine::MaterialCache& matCache,
                          const engine::Camera& camera);

    void update(World& world, float dt) override;

    /// 静态绘制命令（面 + 边线 + 半透明）
    std::span<const MeshDrawCommand> staticFaceCommands() const;
    std::span<const MeshDrawCommand> staticEdgeCommands() const;
    std::span<const MeshDrawCommand> staticTransparentCommands() const;

    /// 动态绘制命令（每帧重建）
    std::span<const MeshDrawCommand> dynamicFaceCommands() const;
    std::span<const MeshDrawCommand> dynamicEdgeCommands() const;

    /// 设置 PSO（由 Viewport 在 initRendering 后调用）
    void setFacePso(engine::PipelineState* pso) { face_pso_ = pso; }
    void setEdgePso(engine::PipelineState* pso) { edge_pso_ = pso; }
    bool hasFacePso() const { return face_pso_ != nullptr; }
    bool hasEdgePso() const { return edge_pso_ != nullptr; }

    engine::GpuResourceManager& gpu() { return gpu_; }

private:
    void processCreated(World& world);
    void processDestroyed(World& world);
    void processDirty(World& world);
    void rebuildStaticList();
    void rebuildDynamicList();

    engine::GpuResourceManager& gpu_;
    engine::MaterialCache&     mat_cache_;
    const engine::Camera&      camera_;

    // SceneProxy 管理
    std::unordered_map<Entity::Id, std::unique_ptr<SceneProxy>> proxies_;
    std::vector<SceneProxy*> proxy_ptrs_;        // 所有 proxy 指针（供遍历）
    std::vector<SceneProxy*> dirty_proxies_;     // 本帧脏 proxy

    // 绘制列表
    StaticDrawList  static_list_;
    DynamicDrawList dynamic_list_;

    // PSO（由 Viewport 提供）
    engine::PipelineState* face_pso_ = nullptr;
    engine::PipelineState* edge_pso_ = nullptr;

    bool needs_full_rebuild_ = true;
};

} // namespace mulan::world
