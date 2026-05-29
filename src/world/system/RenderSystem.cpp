/**
 * @file RenderSystem.cpp
 * @brief 渲染 System 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "RenderSystem.h"
#include "../World.h"

#include <unordered_map>

namespace mulan::world {

RenderSystem::RenderSystem(engine::GpuResourceManager& gpu)
    : System(0), m_gpu(gpu) {}

// ─── update ────────────────────────────────────────────────────

void RenderSystem::update(World& world, float /*dt*/) {
    m_collector.clear();
    m_drawBatches.clear();

    // ── Destroyed ──
    world.forEachDirty(EntityDirty::Destroyed, [&](Entity* e, uint64_t) {
        m_gpu.releaseResource(e->id());
    });

    // ── Geometry + Material → 填 RenderCollector + 上传 GPU ──
    world.forEachDirty(EntityDirty::Geometry | EntityDirty::Material, [&](Entity* e, uint64_t) {
        auto* geo = e->geometry();
        if (!geo) return;

        switch (geo->type()) {
        case GeometryData::Type::Box:
        case GeometryData::Type::Cylinder:
        case GeometryData::Type::Sphere:
        case GeometryData::Type::Solid:
        case GeometryData::Type::Mesh:
            m_collector.addMesh(e->id(), e->cachedFaceMesh(), e->worldTransform(),
                                e->materialId());
            m_collector.addEdges(e->id(), e->cachedEdgeMesh(), e->worldTransform(),
                                 e->materialId());
            m_gpu.uploadFaceMesh(e->id(), e->cachedFaceMesh());
            m_gpu.uploadEdgeMesh(e->id(), e->cachedEdgeMesh());
            break;
        case GeometryData::Type::Line:
        case GeometryData::Type::Arc:
        case GeometryData::Type::Polyline:
            m_collector.addEdges(e->id(), e->cachedEdgeMesh(), e->worldTransform(),
                                 e->materialId());
            m_gpu.uploadEdgeMesh(e->id(), e->cachedEdgeMesh());
            break;
        case GeometryData::Type::PointCloud:
            m_collector.addPoints(e->id(), e->cachedFaceMesh(), e->worldTransform(),
                                  e->materialId());
            m_gpu.uploadFaceMesh(e->id(), e->cachedFaceMesh());
            break;
        default:
            break;
        }
    });

    // ── 产出 DrawBatch（按 materialId 分组可见 Entity）──
    std::unordered_map<uint16_t, std::vector<engine::DrawKey>> groups;
    world.forEachEntity([&](Entity* e) {
        if (!e->geometry() || !e->visible()) return;
        if (!m_gpu.hasResource(e->id())) return;
        engine::DrawKey dk;
        dk.key            = e->id();
        dk.worldTransform = e->worldTransform();
        groups[e->materialId()].push_back(dk);
    });

    for (auto& [matId, keys] : groups) {
        engine::DrawBatch batch;
        batch.materialId = matId;
        batch.keys = std::move(keys);
        m_drawBatches.push_back(std::move(batch));
    }

    // ── Transform only ──
    world.forEachDirty(EntityDirty::Transform, [&](Entity* e, uint64_t flags) {
        if (!hasDirty(flags, EntityDirty::Geometry)) {
            (void)e; // 后期：更新 uniform
        }
    });
}

} // namespace mulan::world
