/**
 * @file render_scene.h
 * @brief RenderScene —— Scene 与 AssetLibrary 派生出的渲染缓存边界
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <cstddef>

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
    };

    RenderScene() = default;

    RenderScene(const RenderScene&) = delete;
    RenderScene& operator=(const RenderScene&) = delete;

    void sync(const scene::Scene& scene, const asset::AssetLibrary& assets);
    void clear();

    const SyncStats& lastSyncStats() const { return last_sync_stats_; }

private:
    SyncStats last_sync_stats_;
};

} // namespace mulan::render_scene

