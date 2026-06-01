/**
 * @file DynamicDrawList.h
 * @brief 动态物体绘制列表 — 每帧重建，用于动态/选中/高亮实体
 *
 * 对应 UE5 的 FDynamicMeshDrawCommand。
 * 与 StaticDrawList 的区别：
 *   - 每帧完全重建，不做增量更新
 *   - 不做 instancing 合批
 *   - 选中高亮、Transform handle 等临时渲染
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include "MeshDrawCommand.h"

#include <span>
#include <vector>

namespace mulan::engine {
class GpuResourceManager;
class PipelineState;
}

namespace mulan::world {

class SceneProxy;

class DynamicDrawList {
public:
    DynamicDrawList() = default;

    /// 每帧重建：为给定的动态 Proxy 生成 MeshDrawCommand
    void rebuild(std::span<SceneProxy* const> dynamicProxies,
                 engine::GpuResourceManager& gpu,
                 engine::PipelineState* facePso,
                 engine::PipelineState* edgePso);

    void clear();

    std::span<const MeshDrawCommand> faceCommands() const { return m_faceCmds; }
    std::span<const MeshDrawCommand> edgeCommands() const { return m_edgeCmds; }

private:
    MeshDrawCommand buildFaceCmd(const SceneProxy& proxy, int lod, int smIdx,
                                  engine::GpuResourceManager& gpu);
    MeshDrawCommand buildEdgeCmd(const SceneProxy& proxy,
                                  engine::GpuResourceManager& gpu);

    std::vector<MeshDrawCommand> m_faceCmds;
    std::vector<MeshDrawCommand> m_edgeCmds;
};

} // namespace mulan::world
