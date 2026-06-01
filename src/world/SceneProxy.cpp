/**
 * @file SceneProxy.cpp
 * @brief SceneProxy 实现 — 从 Entity/GeometryData 构建 SubMesh + 上传 GPU
 * @author hxxcxx
 * @date 2026-06-01
 */

#include "SceneProxy.h"
#include "GeometryData.h"

#include "mulan/engine/render/GpuResourceManager.h"
#include "mulan/engine/geometry/Mesh.h"

namespace mulan::world {

SceneProxy::SceneProxy(Entity::Id entityId)
    : m_entityId(entityId) {
    m_lods.emplace_back();  // LOD 0
}

SceneProxy::~SceneProxy() = default;

bool SceneProxy::hasRenderData() const {
    for (auto& lod : m_lods) {
        for (auto& sm : lod.subMeshes) {
            if (sm.indexCount > 0) return true;
        }
    }
    return false;
}

uint64_t SceneProxy::subMeshGpuKey(int lod, int subMeshIdx) const {
    // section index = sum of submeshes in prior LODs + subMeshIdx
    int sectionIdx = subMeshIdx;
    for (int i = 0; i < lod && i < static_cast<int>(m_lods.size()); ++i) {
        sectionIdx += static_cast<int>(m_lods[i].subMeshes.size());
    }
    return makeGpuKey(m_entityId, sectionIdx);
}

uint64_t SceneProxy::edgeGpuKey() const {
    // Edge 用 sectionIdx = 0xFF 与所有 face SubMesh 区分
    return makeGpuKey(m_entityId, 0xFF);
}

uint64_t SceneProxy::makeGpuKey(Entity::Id id, int subMeshIdx) {
    return (id << 8) | (static_cast<uint64_t>(subMeshIdx) & 0xFF);
}

// ============================================================
// updateFromEntity
// ============================================================

void SceneProxy::updateFromEntity(const Entity& entity, engine::GpuResourceManager& gpu) {
    m_worldTransform = entity.worldTransform();
    m_materialId = entity.materialId();
    m_selected = entity.selected();
    m_visible = entity.visible();

    auto* geo = entity.geometry();
    if (!geo) return;

    // bounds
    auto localBounds = geo->bounds();
    m_bounds = localBounds.transformed(m_worldTransform);

    // 重建 SubMesh + 上传 GPU
    buildSubMeshes(*geo, gpu);
}

void SceneProxy::buildSubMeshes(const GeometryData& geo, engine::GpuResourceManager& gpu) {
    m_lods.clear();
    m_lods.emplace_back();  // LOD 0

    auto faceMesh = geo.faceMesh();
    if (faceMesh.indexCount() > 0) {
        // 当前简化：整个 mesh 作为 1 个 SubMesh
        // 未来：从 GeometryData 获取多 section 信息
        uploadMesh(faceMesh, subMeshGpuKey(0, 0), gpu);

        SubMesh sm;
        sm.indexCount  = static_cast<uint32_t>(faceMesh.indexCount());
        sm.firstIndex  = 0;
        sm.vertexOffset = 0;
        sm.materialId  = m_materialId;
        m_lods[0].subMeshes.push_back(sm);
    }

    // 边线
    auto edgeMesh = geo.edgeMesh();
    if (edgeMesh.indexCount() > 0) {
        uploadMesh(edgeMesh, edgeGpuKey(), gpu);

        m_edgeSubMesh.indexCount  = static_cast<uint32_t>(edgeMesh.indexCount());
        m_edgeSubMesh.firstIndex  = 0;
        m_edgeSubMesh.vertexOffset = 0;
    }
}

void SceneProxy::uploadMesh(const engine::Mesh& mesh, uint64_t key,
                             engine::GpuResourceManager& gpu) {
    if (mesh.indexCount() == 0) return;

    // 区分 face / edge：key 的 sectionIdx 0xFF 表示 edge
    if ((key & 0xFF) == 0xFF) {
        gpu.uploadEdgeMesh(key, mesh);
    } else {
        gpu.uploadFaceMesh(key, mesh);
    }
}

} // namespace mulan::world
