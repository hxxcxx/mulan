/**
 * @file RenderSystem.cpp
 * @brief 渲染 System 实现 — SceneProxy + StaticDrawList + DynamicDrawList
 * @author hxxcxx
 * @date 2026-05-29 (原始) / 2026-06-01 (Phase 3 重写)
 */

#include "RenderSystem.h"
#include "../World.h"

#include <algorithm>

namespace mulan::world {

RenderSystem::RenderSystem(engine::GpuResourceManager& gpu, const engine::Camera& camera)
    : System(0), m_gpu(gpu), m_camera(camera) {}

// ─── update ────────────────────────────────────────────────────

void RenderSystem::update(World& world, float /*dt*/) {
    processCreated(world);
    processDestroyed(world);
    processDirty(world);

    // 重建绘制列表
    if (m_needsFullRebuild) {
        rebuildStaticList();
        m_needsFullRebuild = false;
    } else if (!m_dirtyProxies.empty()) {
        m_staticList.updateDirty(m_dirtyProxies, m_gpu);
    }
    rebuildDynamicList();

    m_dirtyProxies.clear();
}

// ─── Process Created ───────────────────────────────────────────

void RenderSystem::processCreated(World& world) {
    world.forEachDirty(EntityDirty::Created, [&](Entity* e, uint64_t) {
        auto proxy = std::make_unique<SceneProxy>(e->id());
        proxy->updateFromEntity(*e, m_gpu);
        m_proxyPtrs.push_back(proxy.get());
        m_proxies[e->id()] = std::move(proxy);
        m_needsFullRebuild = true;
    });
}

// ─── Process Destroyed ─────────────────────────────────────────

void RenderSystem::processDestroyed(World& world) {
    world.forEachDirty(EntityDirty::Destroyed, [&](Entity* e, uint64_t) {
        auto it = m_proxies.find(e->id());
        if (it == m_proxies.end()) return;

        // 从指针列表移除
        auto* ptr = it->second.get();
        m_proxyPtrs.erase(
            std::remove(m_proxyPtrs.begin(), m_proxyPtrs.end(), ptr),
            m_proxyPtrs.end());

        // 释放 GPU 资源（proxy 析构时由 key 管理，此处释放旧 key）
        m_gpu.releaseResource(e->id());

        m_proxies.erase(it);
        m_needsFullRebuild = true;
    });
}

// ─── Process Dirty ─────────────────────────────────────────────

void RenderSystem::processDirty(World& world) {
    auto renderMask = EntityDirty::Geometry | EntityDirty::Material
                    | EntityDirty::Transform | EntityDirty::Visibility
                    | EntityDirty::Selection;

    world.forEachDirty(renderMask, [&](Entity* e, uint64_t flags) {
        auto it = m_proxies.find(e->id());
        if (it == m_proxies.end()) return;

        auto* proxy = it->second.get();

        if (hasDirty(flags, EntityDirty::Geometry) ||
            hasDirty(flags, EntityDirty::Material)) {
            proxy->updateFromEntity(*e, m_gpu);
            proxy->markDirty(SceneProxy::Dirty::All);
        } else {
            if (hasDirty(flags, EntityDirty::Transform))
                proxy->markDirty(SceneProxy::Dirty::Transform);
            if (hasDirty(flags, EntityDirty::Visibility) ||
                hasDirty(flags, EntityDirty::Selection))
                proxy->markDirty(SceneProxy::Dirty::State);
        }

        m_dirtyProxies.push_back(proxy);
    });
}

// ─── Rebuild Static ────────────────────────────────────────────

void RenderSystem::rebuildStaticList() {
    // 视锥裁剪
    auto frustum = m_camera.frustum();
    std::vector<SceneProxy*> visible;
    for (auto* proxy : m_proxyPtrs) {
        if (!proxy->visible()) continue;
        if (!frustum.intersects(proxy->bounds())) continue;
        visible.push_back(proxy);
    }

    m_staticList.rebuild(visible, m_gpu, m_facePso, m_edgePso);
}

// ─── Rebuild Dynamic ──────────────────────────────────────────

void RenderSystem::rebuildDynamicList() {
    // 动态列表 = 脏 proxy（在 static 更新后仍需独立渲染，如选中高亮叠加）
    // Phase 3 简化版：暂不额外处理，dynamic list 留空
    // 未来：选中 Entity 的叠加高亮、Transform gizmo 等放入此处
    m_dynamicList.clear();
}

// ─── Accessors ─────────────────────────────────────────────────

std::span<const MeshDrawCommand> RenderSystem::staticFaceCommands() const {
    return m_staticList.faceCommands();
}

std::span<const MeshDrawCommand> RenderSystem::staticEdgeCommands() const {
    return m_staticList.edgeCommands();
}

std::span<const MeshDrawCommand> RenderSystem::staticTransparentCommands() const {
    return m_staticList.transparentCommands();
}

std::span<const MeshDrawCommand> RenderSystem::dynamicFaceCommands() const {
    return m_dynamicList.faceCommands();
}

std::span<const MeshDrawCommand> RenderSystem::dynamicEdgeCommands() const {
    return m_dynamicList.edgeCommands();
}

} // namespace mulan::world
