/**
 * @file render_scene.h
 * @brief RenderScene 从 Scene 和 AssetLibrary 派生出的渲染缓存边界
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "scene_proxy.h"

#include <cstddef>
#include <unordered_map>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::scene {
class Scene;
}

namespace mulan::render_scene {

class RenderScene {
public:
    struct SyncStats {
        size_t entityCount = 0;
        size_t assetCount = 0;
        size_t proxyCount = 0;
        size_t visibleProxyCount = 0;
        size_t missingGeometryCount = 0;
    };

    RenderScene() = default;

    RenderScene(const RenderScene&) = delete;
    RenderScene& operator=(const RenderScene&) = delete;

    void sync(const scene::Scene& scene, const asset::AssetLibrary& assets);
    void clear();

    const SyncStats& lastSyncStats() const { return last_sync_stats_; }
    size_t proxyCount() const { return proxies_.size(); }
    const SceneProxy* proxy(scene::EntityId id) const;
    const engine::AABB& sceneBounds() const { return scene_bounds_; }

    template<typename Func>
    void forEachProxy(Func&& fn) const {
        for (const auto& [id, proxy] : proxies_)
            fn(proxy);
    }

private:
    SyncStats last_sync_stats_;
    engine::AABB scene_bounds_;
    std::unordered_map<scene::EntityId, SceneProxy> proxies_;
};

} // namespace mulan::render_scene
