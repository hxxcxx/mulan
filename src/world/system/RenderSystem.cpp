/**
 * @file RenderSystem.cpp
 * @brief 渲染 System 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "RenderSystem.h"
#include "../World.h"

namespace mulan::world {

RenderSystem::RenderSystem() : System(0) {}

void RenderSystem::update(World& world, float /*dt*/) {
    m_collector.clear();

    // ── Destroyed ──
    world.forEachDirty(EntityDirty::Destroyed, [&](Entity* e, uint64_t) {
        // 后期接入：m_gpu.releaseResource(e->id());
        (void)e;
    });

    // ── Geometry ──
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
            break;
        case GeometryData::Type::Line:
        case GeometryData::Type::Arc:
        case GeometryData::Type::Polyline:
            m_collector.addEdges(e->id(), e->cachedEdgeMesh(), e->worldTransform(),
                                 e->materialId());
            break;
        case GeometryData::Type::PointCloud:
            m_collector.addPoints(e->id(), e->cachedFaceMesh(), e->worldTransform(),
                                  e->materialId());
            break;
        default:
            break;
        }
    });

    // ── Transform（仅 transform 变化，几何没变）──
    world.forEachDirty(EntityDirty::Transform, [&](Entity* e, uint64_t flags) {
        if (!hasDirty(flags, EntityDirty::Geometry)) {
            // 后期接入：m_gpu.updateTransform(e->id(), e->worldTransform());
            (void)e;
        }
    });

    // ── 后期接入：m_collector.flush(m_gpu) ──
}

} // namespace mulan::world
