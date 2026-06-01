/**
 * @file SceneProxy.h
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

#include "SubMesh.h"
#include "Entity.h"

#include "mulan/engine/math/Math.h"
#include "mulan/engine/math/AABB.h"

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

    Entity::Id entityId() const { return m_entityId; }

    // --- 从 Entity 同步（RenderSystem::update 调用）---

    void updateFromEntity(const Entity& entity, engine::GpuResourceManager& gpu);

    // --- LOD / SubMesh ---

    int lodCount() const { return static_cast<int>(m_lods.size()); }
    const LodLevel& lod(int level) const { return m_lods[level]; }

    const EdgeSubMesh& edgeSubMesh() const { return m_edgeSubMesh; }
    bool hasEdgeData() const { return m_edgeSubMesh.indexCount > 0; }
    bool hasRenderData() const;

    // --- 同步字段 ---

    const engine::Mat4& worldTransform() const { return m_worldTransform; }
    uint16_t materialId() const { return m_materialId; }
    bool selected() const { return m_selected; }
    bool visible() const { return m_visible; }
    const engine::AABB& bounds() const { return m_bounds; }

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

    Dirty dirty() const { return m_dirty; }
    void markDirty(Dirty d) { m_dirty = m_dirty | d; }
    void clearDirty() { m_dirty = Dirty::None; }

private:
    void buildSubMeshes(const GeometryData& geo, engine::GpuResourceManager& gpu);
    void uploadMesh(const engine::Mesh& mesh, uint64_t key,
                    engine::GpuResourceManager& gpu);

    Entity::Id             m_entityId;
    std::vector<LodLevel>  m_lods;          // LOD[0..N]
    EdgeSubMesh            m_edgeSubMesh;

    engine::Mat4  m_worldTransform{1.0};
    uint16_t      m_materialId = 0xFFFF;
    bool          m_selected   = false;
    bool          m_visible    = true;
    engine::AABB  m_bounds;
    Dirty         m_dirty = Dirty::All;
};

} // namespace mulan::world
