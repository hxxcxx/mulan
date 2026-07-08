/**
 * @file render_scene.h
 * @brief RenderScene 从 Scene 和 AssetLibrary 派生出的渲染缓存边界
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "scene_proxy.h"

#include <mulan/engine/render/light_environment.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::scene {
class Scene;
}

namespace mulan::view {

class RenderScene {
public:
    enum class PickHitKind : uint8_t {
        None,
        Object,
        Vertex,
        Edge,
        Face,
        Curve,
    };

    struct PickResult {
        scene::EntityId entity;
        uint32_t pickId = 0;
        double distance = 0.0;
        PickHitKind kind = PickHitKind::Object;
        math::Point3 worldPoint;
        bool hasWorldPoint = false;
        math::Vec3 worldNormal;
        bool hasWorldNormal = false;
        size_t sourceDrawableIndex = 0;
        size_t primitiveIndex = 0;
        bool hasPrimitiveIndex = false;
        double parameter = 0.0;
        double toleranceWorld = 0.0;
        math::Point3 edgeStart;
        math::Point3 edgeEnd;
        bool hasEdgeSegment = false;
        math::Vec3 barycentric;
        bool hasBarycentric = false;
    };

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

    /// 同步 Scene 变更到渲染缓存。
    /// 首次调用做全量重建；之后只消费 Scene 的 EntityDirty 增量更新
    /// （并 clearDirty 已消费的脏位）。可通过 resetSync() 强制下次全量。
    void sync(scene::Scene& scene, const asset::AssetLibrary& assets);

    /// 强制下次 sync() 走全量重建（如资产库整体替换后）。
    void resetSync() { initialized_ = false; }

    void clear();

    const SyncStats& lastSyncStats() const { return last_sync_stats_; }
    size_t proxyCount() const { return proxies_.size(); }
    const SceneProxy* proxy(scene::EntityId id) const;
    std::optional<PickResult> pick(const math::Ray3& ray, double lineToleranceWorld = 0.0) const;
    void collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld, std::vector<PickResult>& out) const;
    const math::AABB3& sceneBounds() const { return scene_bounds_; }
    const std::vector<engine::Light>& lights() const { return lights_; }

    template <typename Func>
    void forEachProxy(Func&& fn) const {
        for (const auto& [id, proxy] : proxies_)
            fn(proxy);
    }

private:
    SyncStats last_sync_stats_;
    math::AABB3 scene_bounds_;
    std::unordered_map<scene::EntityId, SceneProxy> proxies_;
    std::vector<engine::Light> lights_;
    const asset::AssetLibrary* assets_ = nullptr;
    bool initialized_ = false;  // 首次 sync 全量，之后增量
};

}  // namespace mulan::view
