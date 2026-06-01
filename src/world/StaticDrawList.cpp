/**
 * @file StaticDrawList.cpp
 * @brief StaticDrawList 实现 — SubMesh → MeshDrawCommand
 * @author hxxcxx
 * @date 2026-06-01
 *
 * Phase 3 简化版：每 Entity 每 SubMesh 一个 MeshDrawCommand（不 instancing）。
 * MeshBatch 合批优化在后续迭代中完成。
 */

#include "StaticDrawList.h"
#include "Entity.h"
#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/render/MeshDrawCommand.h"

namespace mulan::world {

void StaticDrawList::rebuild(std::span<SceneProxy* const> proxies,
                              engine::GpuResourceManager& gpu,
                              engine::PipelineState* facePso,
                              engine::PipelineState* /*edgePso*/) {
    clear();

    uint32_t nextObjectOffset = 0;  // 跟踪 object UBO 分配

    for (auto* proxy : proxies) {
        if (!proxy || !proxy->visible() || !proxy->hasRenderData()) continue;

        ProxyIndex idx;
        idx.faceStart = m_faceCmds.size();

        for (int lod = 0; lod < proxy->lodCount(); ++lod) {
            auto& level = proxy->lod(lod);
            for (int si = 0; si < static_cast<int>(level.subMeshes.size()); ++si) {
                auto cmd = buildFaceSubMeshCmd(*proxy, lod, si, gpu);
                cmd.pipelineState = facePso;
                cmd.objectUboOffset = nextObjectOffset;
                cmd.worldTransform  = proxy->worldTransform();
                m_faceCmds.push_back(std::move(cmd));
                nextObjectOffset += MeshDrawCommand::kObjectUboStride;
            }
        }
        idx.faceCount = m_faceCmds.size() - idx.faceStart;

        idx.edgeStart = m_edgeCmds.size();
        if (proxy->hasEdgeData()) {
            auto cmd = buildEdgeCmd(*proxy, gpu);
            cmd.objectUboOffset = nextObjectOffset;
            cmd.worldTransform  = proxy->worldTransform();
            m_edgeCmds.push_back(std::move(cmd));
            idx.edgeCount = 1;
            nextObjectOffset += MeshDrawCommand::kObjectUboStride;
        }

        m_proxyIndex[proxy->entityId()] = idx;
    }
}

void StaticDrawList::updateDirty(std::span<SceneProxy* const> dirtyProxies,
                                  engine::GpuResourceManager& gpu) {
    for (auto* proxy : dirtyProxies) {
        if (!proxy || !proxy->visible()) continue;
        rebuildProxy(*proxy, gpu);
    }
}

void StaticDrawList::clear() {
    m_faceCmds.clear();
    m_edgeCmds.clear();
    m_transparentCmds.clear();
    m_proxyIndex.clear();
}

// ============================================================
// gatherIntoBatches / buildCommands — 预留，Phase 3 简化版未使用
// ============================================================

void StaticDrawList::gatherIntoBatches(std::span<SceneProxy* const>) {}
void StaticDrawList::buildCommands(engine::GpuResourceManager&,
                                    engine::PipelineState*,
                                    engine::PipelineState*) {}

// ============================================================
// rebuildProxy
// ============================================================

void StaticDrawList::rebuildProxy(const SceneProxy& proxy,
                                   engine::GpuResourceManager& gpu) {
    auto it = m_proxyIndex.find(proxy.entityId());
    if (it == m_proxyIndex.end()) return;

    auto& idx = it->second;
    size_t cmdPos = idx.faceStart;
    for (int lod = 0; lod < proxy.lodCount(); ++lod) {
        auto& level = proxy.lod(lod);
        for (int si = 0; si < static_cast<int>(level.subMeshes.size()); ++si) {
            if (cmdPos < m_faceCmds.size()) {
                auto* faceGeo = gpu.faceGeometry(proxy.subMeshGpuKey(lod, si));
                if (faceGeo) {
                    auto& cmd = m_faceCmds[cmdPos];
                    cmd.vertexBuffer  = faceGeo->vertexBuffer.get();
                    cmd.indexBuffer   = faceGeo->indexBuffer.get();
                    cmd.indexCount    = level.subMeshes[si].indexCount;
                    cmd.firstIndex    = level.subMeshes[si].firstIndex;
                    cmd.baseVertex    = static_cast<int32_t>(level.subMeshes[si].vertexOffset);
                    cmd.selected      = proxy.selected();
                    cmd.worldTransform = proxy.worldTransform();
                }
            }
            ++cmdPos;
        }
    }

    if (proxy.hasEdgeData() && idx.edgeCount > 0 && idx.edgeStart < m_edgeCmds.size()) {
        auto* edgeGeo = gpu.edgeGeometry(proxy.edgeGpuKey());
        if (edgeGeo) {
            auto& cmd = m_edgeCmds[idx.edgeStart];
            cmd.vertexBuffer = edgeGeo->vertexBuffer.get();
            cmd.indexBuffer  = edgeGeo->indexBuffer.get();
            cmd.selected     = proxy.selected();
            cmd.worldTransform = proxy.worldTransform();
        }
    }
}

MeshDrawCommand StaticDrawList::buildFaceSubMeshCmd(const SceneProxy& proxy,
                                                      int lod, int smIdx,
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

MeshDrawCommand StaticDrawList::buildEdgeCmd(const SceneProxy& proxy,
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
