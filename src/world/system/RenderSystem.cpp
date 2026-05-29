/**
 * @file RenderSystem.cpp
 * @brief 渲染 System 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "RenderSystem.h"
#include "../World.h"

namespace mulan::world {

RenderSystem::RenderSystem(engine::GpuResourceManager& gpu)
    : System(0), m_gpu(gpu) {}

void RenderSystem::update(World& world, float /*dt*/) {
    m_collector.clear();

    // ── Destroyed ──
    world.forEachDirty(EntityDirty::Destroyed, [&](Entity* e, uint64_t) {
        m_gpu.releaseResource(e->id());
    });

    // ── Geometry + Material ──
    world.forEachDirty(EntityDirty::Geometry | EntityDirty::Material, [&](Entity* e, uint64_t) {
        auto* geo = e->geometry();
        if (!geo) return;

        switch (geo->type()) {
        case GeometryData::Type::Box:
        case GeometryData::Type::Cylinder:
        case GeometryData::Type::Sphere:
        case GeometryData::Type::Solid:
        case GeometryData::Type::Mesh:
            m_gpu.uploadFaceMesh(e->id(), e->cachedFaceMesh());
            m_gpu.uploadEdgeMesh(e->id(), e->cachedEdgeMesh());
            break;
        case GeometryData::Type::Line:
        case GeometryData::Type::Arc:
        case GeometryData::Type::Polyline:
            m_gpu.uploadEdgeMesh(e->id(), e->cachedEdgeMesh());
            break;
        case GeometryData::Type::PointCloud:
            m_gpu.uploadFaceMesh(e->id(), e->cachedFaceMesh());
            break;
        default:
            break;
        }
    });

    // ── Transform only（几何没变，后期接入 uniform 更新）──
    world.forEachDirty(EntityDirty::Transform, [&](Entity* e, uint64_t flags) {
        if (!hasDirty(flags, EntityDirty::Geometry)) {
            // 后期接入：更新 per-draw-call transform uniform
            (void)e;
        }
    });
}

} // namespace mulan::world
