/**
 * @file StaticDrawList.h
 * @brief 静态物体绘制列表 — SubMesh → MeshBatch → MeshDrawCommand
 *
 * 对应 UE5 的 FMeshDrawCommand 缓存。
 * 两阶段合批：
 *   1. gatherIntoBatches() — 遍历 proxies 的 SubMesh，按 PipelineKey 归入 MeshBatch
 *   2. buildCommands() — 每个 MeshBatch → 一个 instanced MeshDrawCommand
 *
 * 增量更新：updateDirty() 定位受影响的 commands 并就地重建。
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include <mulan/engine/render/mesh_draw_command.h>
#include "sub_mesh.h"
#include "scene_proxy.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace mulan::engine {
class GpuResourceManager;
class MaterialCache;
}

namespace mulan::world {

using engine::MeshDrawCommand;

class StaticDrawList {
public:
    StaticDrawList() = default;

    /// 全量重建（场景加载或大规模变更时）
    /// @param pso 由调用方（ForwardPass）提供 PSO 指针
    void rebuild(std::span<SceneProxy* const> proxies,
                 engine::GpuResourceManager& gpu,
                 engine::MaterialCache& matCache,
                 engine::PipelineState* facePso,
                 engine::PipelineState* edgePso);

    /// 增量更新脏 Proxy 的 commands
    void updateDirty(std::span<SceneProxy* const> dirtyProxies,
                     engine::GpuResourceManager& gpu);

    void clear();

    std::span<const MeshDrawCommand> faceCommands() const { return face_cmds_; }
    std::span<const MeshDrawCommand> edgeCommands() const { return edge_cmds_; }
    std::span<const MeshDrawCommand> transparentCommands() const { return transparent_cmds_; }

private:
    /// 阶段1：收集 SubMesh → 按 PipelineKey 归入 MeshBatch
    void gatherIntoBatches(std::span<SceneProxy* const> proxies);

    /// 阶段2：每个 MeshBatch → 一个 instanced MeshDrawCommand
    void buildCommands(engine::GpuResourceManager& gpu,
                       engine::PipelineState* facePso,
                       engine::PipelineState* edgePso);

    /// 重建单个 Proxy 贡献的所有 commands
    void rebuildProxy(const SceneProxy& proxy, engine::GpuResourceManager& gpu);

    /// 为单个 SubMesh 构建 MeshDrawCommand（非 instanced）
    MeshDrawCommand buildFaceSubMeshCmd(const SceneProxy& proxy, int lod, int smIdx,
                                         engine::GpuResourceManager& gpu);

    MeshDrawCommand buildEdgeCmd(const SceneProxy& proxy,
                                  engine::GpuResourceManager& gpu);

    std::vector<MeshDrawCommand> face_cmds_;
    std::vector<MeshDrawCommand> edge_cmds_;
    std::vector<MeshDrawCommand> transparent_cmds_;

    // 增量索引：每个 Entity 对应的 command range
    struct ProxyIndex {
        size_t faceStart = 0, faceCount = 0;
        size_t edgeStart = 0, edgeCount = 0;
    };
    std::unordered_map<Entity::Id, ProxyIndex> proxy_index_;
};

} // namespace mulan::world
