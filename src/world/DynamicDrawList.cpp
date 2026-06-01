/**
 * @file DynamicDrawList.cpp
 * @brief DynamicDrawList 实现 — 每帧重建的动态物体绘制列表
 * @author hxxcxx
 * @date 2026-06-01
 */

#include "DynamicDrawList.h"
#include "Entity.h"
#include "SceneProxy.h"
#include "mulan/engine/render/GpuResourceManager.h"

namespace mulan::world {

void DynamicDrawList::rebuild(std::span<SceneProxy* const> dynamicProxies,
                               engine::GpuResourceManager& gpu,
                               engine::PipelineState* facePso,
                               engine::PipelineState* edgePso) {
    clear();

    for (auto* proxy : dynamicProxies) {
        if (!proxy || !proxy->visible() || !proxy->hasRenderData()) continue;

        for (int lod = 0; lod < proxy->lodCount(); ++lod) {
            auto& level = proxy->lod(lod);
            for (int si = 0; si < static_cast<int>(level.subMeshes.size()); ++si) {
                auto cmd = buildFaceCmd(*proxy, lod, si, gpu);
                cmd.pipelineState = facePso;
                m_faceCmds.push_back(std::move(cmd));
            }
        }

        if (proxy->hasEdgeData()) {
            auto cmd = buildEdgeCmd(*proxy, gpu);
            cmd.pipelineState = edgePso;
            m_edgeCmds.push_back(std::move(cmd));
        }
    }
}

void DynamicDrawList::clear() {
    m_faceCmds.clear();
    m_edgeCmds.clear();
}

MeshDrawCommand DynamicDrawList::buildFaceCmd(const SceneProxy& proxy, int lod,
                                               int smIdx,
                                               engine::GpuResourceManager& gpu) {
    MeshDrawCommand cmd;
    auto* faceGeo = gpu.faceGeometry(proxy.subMeshGpuKey(lod, smIdx));
    if (!faceGeo) return cmd;

    auto& sm = proxy.lod(lod).subMeshes[smIdx];
    cmd.vertexBuffer  = faceGeo->vertexBuffer.get();
    cmd.indexBuffer   = faceGeo->indexBuffer.get();
    cmd.indexCount    = sm.indexCount;
    cmd.firstIndex    = sm.firstIndex;
    cmd.baseVertex    = static_cast<int32_t>(sm.vertexOffset);
    cmd.instanceCount = 1;
    cmd.topology      = sm.topology;
    cmd.pickId        = static_cast<uint32_t>(proxy.entityId() & Entity::INDEX_MASK);
    cmd.selected      = proxy.selected();
    return cmd;
}

MeshDrawCommand DynamicDrawList::buildEdgeCmd(const SceneProxy& proxy,
                                               engine::GpuResourceManager& gpu) {
    MeshDrawCommand cmd;
    auto* edgeGeo = gpu.edgeGeometry(proxy.edgeGpuKey());
    if (!edgeGeo) return cmd;

    auto& es = proxy.edgeSubMesh();
    cmd.vertexBuffer  = edgeGeo->vertexBuffer.get();
    cmd.indexBuffer   = edgeGeo->indexBuffer.get();
    cmd.indexCount    = es.indexCount;
    cmd.firstIndex    = es.firstIndex;
    cmd.baseVertex    = static_cast<int32_t>(es.vertexOffset);
    cmd.instanceCount = 1;
    cmd.topology      = engine::PrimitiveTopology::LineList;
    cmd.isEdge        = true;
    cmd.pickId        = static_cast<uint32_t>(proxy.entityId() & Entity::INDEX_MASK);
    cmd.selected      = proxy.selected();
    return cmd;
}

} // namespace mulan::world
