#include "scene_proxy.h"
#include "geometry_data.h"

#include "mulan/engine/render/gpu_resource_manager.h"
#include "mulan/engine/geometry/mesh.h"

namespace mulan::world {

SceneProxy::SceneProxy(Entity::Id entityId)
    : entity_id_(entityId) {
    lods_.emplace_back();  // LOD 0
}

SceneProxy::~SceneProxy() = default;

bool SceneProxy::hasRenderData() const {
    for (auto& lod : lods_) {
        for (auto& sm : lod.subMeshes) {
            if (sm.indexCount > 0) return true;
        }
    }
    return false;
}

uint64_t SceneProxy::subMeshGpuKey(int lod, int subMeshIdx) const {
    // section index = sum of submeshes in prior LODs + subMeshIdx
    int sectionIdx = subMeshIdx;
    for (int i = 0; i < lod && i < static_cast<int>(lods_.size()); ++i) {
        sectionIdx += static_cast<int>(lods_[i].subMeshes.size());
    }
    return makeGpuKey(entity_id_, sectionIdx);
}

uint64_t SceneProxy::edgeGpuKey() const {
    // Edge 用 sectionIdx = 0xFF 与所有 face SubMesh 区分
    return makeGpuKey(entity_id_, 0xFF);
}

uint64_t SceneProxy::makeGpuKey(Entity::Id id, int subMeshIdx) {
    return (id << 8) | (static_cast<uint64_t>(subMeshIdx) & 0xFF);
}

// ============================================================
// updateFromEntity
// ============================================================

void SceneProxy::updateFromEntity(const Entity& entity, engine::GpuResourceManager& gpu) {
    world_transform_ = entity.worldTransform();
    material_id_ = entity.materialId();
    selected_ = entity.selected();
    visible_ = entity.visible();

    auto* geo = entity.geometry();
    if (!geo) return;

    // bounds
    auto localBounds = geo->bounds();
    bounds_ = localBounds.transformed(world_transform_);

    // 重建 SubMesh + 上传 GPU
    buildSubMeshes(*geo, gpu);
}

void SceneProxy::buildSubMeshes(const GeometryData& geo, engine::GpuResourceManager& gpu) {
    lods_.clear();
    lods_.emplace_back();  // LOD 0

    auto faceMesh = geo.faceMesh();
    if (faceMesh.indexCount() > 0) {
        // 当前简化：整个 mesh 作为 1 个 SubMesh
        // 未来：从 GeometryData 获取多 section 信息
        uploadMesh(faceMesh, subMeshGpuKey(0, 0), gpu);

        SubMesh sm;
        sm.indexCount  = static_cast<uint32_t>(faceMesh.indexCount());
        sm.firstIndex  = 0;
        sm.vertexOffset = 0;
        sm.materialId  = material_id_;
        lods_[0].subMeshes.push_back(sm);
    }

    // 边线
    auto edgeMesh = geo.edgeMesh();
    if (edgeMesh.indexCount() > 0) {
        uploadMesh(edgeMesh, edgeGpuKey(), gpu);

        edge_sub_mesh_.indexCount  = static_cast<uint32_t>(edgeMesh.indexCount());
        edge_sub_mesh_.firstIndex  = 0;
        edge_sub_mesh_.vertexOffset = 0;
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
