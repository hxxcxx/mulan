#include "render_system.h"
#include "../World.h"

#include "mulan/engine/render/material/material_cache.h"

#include <algorithm>

namespace mulan::world {

RenderSystem::RenderSystem(engine::GpuResourceManager& gpu,
                           engine::MaterialCache& matCache,
                           const engine::Camera& camera)
    : System(0), gpu_(gpu), mat_cache_(matCache), camera_(camera) {}

// ─── update ────────────────────────────────────────────────────

void RenderSystem::update(World& world, float /*dt*/) {
    processCreated(world);
    processDestroyed(world);
    processDirty(world);

    // 重建绘制列表
    if (needs_full_rebuild_) {
        rebuildStaticList();
        needs_full_rebuild_ = false;
    } else if (!dirty_proxies_.empty()) {
        static_list_.updateDirty(dirty_proxies_, gpu_);
    }
    rebuildDynamicList();

    dirty_proxies_.clear();
}

// ─── Process Created ───────────────────────────────────────────

void RenderSystem::processCreated(World& world) {
    world.forEachDirty(EntityDirty::Created, [&](Entity* e, uint64_t) {
        auto proxy = std::make_unique<SceneProxy>(e->id());
        proxy->updateFromEntity(*e, gpu_);
        proxy_ptrs_.push_back(proxy.get());
        proxies_[e->id()] = std::move(proxy);
        needs_full_rebuild_ = true;
    });
}

// ─── Process Destroyed ─────────────────────────────────────────

void RenderSystem::processDestroyed(World& world) {
    world.forEachDirty(EntityDirty::Destroyed, [&](Entity* e, uint64_t) {
        auto it = proxies_.find(e->id());
        if (it == proxies_.end()) return;

        // 从指针列表移除
        auto* ptr = it->second.get();
        proxy_ptrs_.erase(
            std::remove(proxy_ptrs_.begin(), proxy_ptrs_.end(), ptr),
            proxy_ptrs_.end());

        // 释放 GPU 资源（proxy 析构时由 key 管理，此处释放旧 key）
        gpu_.releaseResource(e->id());

        proxies_.erase(it);
        needs_full_rebuild_ = true;
    });
}

// ─── Process Dirty ─────────────────────────────────────────────

void RenderSystem::processDirty(World& world) {
    auto renderMask = EntityDirty::Geometry | EntityDirty::Material
                    | EntityDirty::Transform | EntityDirty::Visibility
                    | EntityDirty::Selection;

    world.forEachDirty(renderMask, [&](Entity* e, uint64_t flags) {
        auto it = proxies_.find(e->id());
        if (it == proxies_.end()) return;

        auto* proxy = it->second.get();

        if (hasDirty(flags, EntityDirty::Geometry) ||
            hasDirty(flags, EntityDirty::Material)) {
            proxy->updateFromEntity(*e, gpu_);
            proxy->markDirty(SceneProxy::Dirty::All);
        } else {
            if (hasDirty(flags, EntityDirty::Transform))
                proxy->markDirty(SceneProxy::Dirty::Transform);
            if (hasDirty(flags, EntityDirty::Visibility) ||
                hasDirty(flags, EntityDirty::Selection))
                proxy->markDirty(SceneProxy::Dirty::State);
        }

        dirty_proxies_.push_back(proxy);
    });
}

// ─── Rebuild Static ────────────────────────────────────────────

void RenderSystem::rebuildStaticList() {
    // 视锥裁剪
    auto frustum = camera_.frustum();
    std::vector<SceneProxy*> visible;
    for (auto* proxy : proxy_ptrs_) {
        if (!proxy->visible()) continue;
        if (!frustum.intersects(proxy->bounds())) continue;
        visible.push_back(proxy);
    }

    static_list_.rebuild(visible, gpu_, mat_cache_, face_pso_, edge_pso_);
}

// ─── Rebuild Dynamic ──────────────────────────────────────────

void RenderSystem::rebuildDynamicList() {
    // 动态列表 = 脏 proxy（在 static 更新后仍需独立渲染，如选中高亮叠加）
    // Phase 3 简化版：暂不额外处理，dynamic list 留空
    // 未来：选中 Entity 的叠加高亮、Transform gizmo 等放入此处
    dynamic_list_.clear();
}

// ─── Accessors ─────────────────────────────────────────────────

std::span<const MeshDrawCommand> RenderSystem::staticFaceCommands() const {
    return static_list_.faceCommands();
}

std::span<const MeshDrawCommand> RenderSystem::staticEdgeCommands() const {
    return static_list_.edgeCommands();
}

std::span<const MeshDrawCommand> RenderSystem::staticTransparentCommands() const {
    return static_list_.transparentCommands();
}

std::span<const MeshDrawCommand> RenderSystem::dynamicFaceCommands() const {
    return dynamic_list_.faceCommands();
}

std::span<const MeshDrawCommand> RenderSystem::dynamicEdgeCommands() const {
    return dynamic_list_.edgeCommands();
}

} // namespace mulan::world
