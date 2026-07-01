/**
 * @file scene_proxy.h
 * @brief Entity 的渲染线程镜像 — 持有 LOD → SubMesh[] + GPU buffer key
 *
 * 对应 UE5 的 FPrimitiveSceneProxy（简化版，去掉 Scene 中间层）。
 *
 * 设计决策：
 * - 不持有 RHIDevice/Buffer*，通过 GpuResourceManager 的 key 间接引用
 * - Dirty 标记独立于 EntityDirty，是渲染管线内部的
 * - LOD 预留：当前只实现 LOD 0
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include "sub_mesh.h"
#include "entity.h"

#include "mulan/engine/math/math.h"
#include "mulan/engine/math/aabb.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::engine {
class GpuResourceManager;
class Mesh;
}

namespace mulan::world {

class GeometryData;

class SceneProxy {
public:
    explicit SceneProxy(Entity::Id entityId);
    ~SceneProxy();

    Entity::Id entityId() const { return entity_id_; }

    // --- 从 Entity 同步（RenderSystem::update 调用）---

    void updateFromEntity(const Entity& entity, engine::GpuResourceManager& gpu);

    // --- LOD / SubMesh ---

    int lodCount() const { return static_cast<int>(lods_.size()); }
    const LodLevel& lod(int level) const { return lods_[level]; }

    const EdgeSubMesh& edgeSubMesh() const { return edge_sub_mesh_; }
    bool hasEdgeData() const { return edge_sub_mesh_.indexCount > 0; }
    bool hasRenderData() const;

    // --- 同步字段 ---

    const engine::Mat4& worldTransform() const { return world_transform_; }
    uint16_t materialId() const { return material_id_; }
    bool selected() const { return selected_; }
    bool visible() const { return visible_; }
    const engine::AABB& bounds() const { return bounds_; }

    // --- GPU buffer keys（per-subMesh）---

    uint64_t subMeshGpuKey(int lod, int subMeshIdx) const;
    uint64_t edgeGpuKey() const;

    /// (entityId << 8) | subMeshIdx，每个 Entity ≤ 256 SubMesh
    static uint64_t makeGpuKey(Entity::Id id, int subMeshIdx);

    // --- 脏标记 ---

    enum class Dirty : uint8_t {
        None      = 0,
        Transform = 1 << 0,
        Geometry  = 1 << 1,
        Material  = 1 << 2,
        State     = 1 << 3,  // selected / visible
        All       = 0xFF
    };
    friend Dirty operator|(Dirty a, Dirty b) { return Dirty(uint8_t(a) | uint8_t(b)); }
    friend Dirty operator&(Dirty a, Dirty b) { return Dirty(uint8_t(a) & uint8_t(b)); }

    Dirty dirty() const { return dirty_; }
    void markDirty(Dirty d) { dirty_ = dirty_ | d; }
    void clearDirty() { dirty_ = Dirty::None; }

private:
    void buildSubMeshes(const GeometryData& geo, engine::GpuResourceManager& gpu);
    void uploadMesh(const engine::Mesh& mesh, uint64_t key,
                    engine::GpuResourceManager& gpu);

    Entity::Id             entity_id_;
    std::vector<LodLevel>  lods_;          // LOD[0..N]
    EdgeSubMesh            edge_sub_mesh_;
    engine::AABB  cached_bounds_;
    engine::Mat4  world_transform_{1.0};
    uint16_t      material_id_ = 0xFFFF;
    bool          selected_   = false;
    bool          visible_    = true;
    engine::AABB  bounds_;
    Dirty         dirty_ = Dirty::All;
};

} // namespace mulan::world
