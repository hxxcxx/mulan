/**
 * @file render_scene.h
 * @brief RenderScene 从 Scene 和 AssetLibrary 派生出的渲染缓存边界
 * @author hxxcxx
 * @date 2026-07-02 (原始) / 2026-07-15 (变更 journal、场景级 BVH 与拾取统计)
 */

#pragma once

#include <mulan/view/scene_sync/scene_proxy.h>

#include <mulan/render/light_environment.h>
#include <mulan/render/frontend/pick_identity.h>
#include <mulan/scene/scene_change.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::scene {
class Scene;
}

namespace mulan::view {

class GeometryQueryWorld;
namespace detail {
class SceneSpatialIndex;
}

/// 单次拾取查询的轻量宽阶段诊断，不包含任何持久对象引用。
struct PickQueryStats {
    size_t visibleProxyCount = 0;    ///< 查询时可见代理总数。
    size_t indexedProxyCount = 0;    ///< 进入 BVH 的有限非空 bounds 数量。
    size_t fallbackProxyCount = 0;   ///< 必须保守精确测试的空/非有限 bounds 数量。
    size_t nodeBoundsTestCount = 0;  ///< BVH 节点 AABB 测试次数。
    size_t leafBoundsTestCount = 0;  ///< BVH 叶条目 AABB 测试次数。
    size_t candidateProxyCount = 0;  ///< 宽阶段输出的代理数量（含 fallback）。
    size_t exactAssetTestCount = 0;  ///< 实际进入资产级精确拾取的代理数量。
};

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
        engine::PickId pickId;
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
        math::Point3 curveCenter;
        math::Vec3 curveNormal;
        double curveRadius = 0.0;
        bool hasCurveCircle = false;
        math::Point3 curveStart;
        math::Point3 curveEnd;
        math::Point3 curveMidpoint;
        bool hasCurveEndpoints = false;
        bool hasCurveMidpoint = false;
        bool curveClosed = false;
        math::Vec3 curveStartDirection;
        double curveSweepRadians = 0.0;
        bool hasCurveRange = false;
        math::Vec3 barycentric;
        bool hasBarycentric = false;
    };

    struct SyncStats {
        size_t entityCount = 0;
        size_t assetCount = 0;
        size_t proxyCount = 0;
        size_t visibleProxyCount = 0;
        size_t missingGeometryCount = 0;  ///< 持有几何引用但资产缺失/类型错误的当前实体总数。
    };

    RenderScene();
    ~RenderScene();

    RenderScene(const RenderScene&) = delete;
    RenderScene& operator=(const RenderScene&) = delete;

    /// 从只读 Scene 同步渲染缓存。首次调用全量重建，之后通过本对象私有 cursor
    /// 非破坏读取变更 journal；其他 RenderScene 或消费者的读取互不影响。
    void sync(const scene::Scene& scene, const asset::AssetLibrary& assets);

    /// 强制下次 sync() 走全量重建（如资产库整体替换后）。
    void resetSync() { initialized_ = false; }

    void clear();

    const SyncStats& lastSyncStats() const { return last_sync_stats_; }
    /// SceneWorld 投影版本；实体可见性、变换、几何和材质等变化后递增。
    /// 选择由 ViewState 独立下发，灯光由 LightEnvironment 独立下发，二者不推进此版本。
    uint64_t generation() const { return generation_; }
    /// 几何资源版本；仅在 GPU mesh 可能需要重新上传时递增。
    /// 几何代理身份或所引用资产内容变化的诊断世代；实际 GPU 上传由资源差量决定。
    uint64_t geometryGeneration() const { return geometry_generation_; }
    size_t proxyCount() const { return proxies_.size(); }
    const SceneProxy* proxy(scene::EntityId id) const;
    std::optional<PickResult> pick(const math::Ray3& ray, double lineToleranceWorld = 0.0,
                                   PickQueryStats* stats = nullptr) const;
    void collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld, std::vector<PickResult>& out,
                               PickQueryStats* stats = nullptr) const;
    const math::AABB3& sceneBounds() const { return scene_bounds_; }
    /// 由可见实体世界 AABB 汇总得到的保守世界包围球，用于相机裁剪范围。
    const math::Sphere3& sceneBoundsSphere() const { return scene_bounds_sphere_; }
    const std::vector<engine::Light>& lights() const { return lights_; }

    template <typename Func>
    void forEachProxy(Func&& fn) const {
        for (const auto& [id, proxy] : proxies_)
            fn(proxy);
    }

private:
    friend class GeometryQueryWorld;

    void rebuildSpatialIndex();
    engine::PickId pickIdForEntity(scene::EntityId entity);

    SyncStats last_sync_stats_;
    math::AABB3 scene_bounds_;
    math::Sphere3 scene_bounds_sphere_;
    std::unordered_map<scene::EntityId, SceneProxy> proxies_;
    std::unordered_map<scene::EntityId, uint64_t> geometry_asset_revisions_;
    std::unordered_map<scene::EntityId, engine::PickId> entity_pick_ids_;
    std::unordered_set<scene::EntityId> missing_geometry_entities_;
    std::unordered_set<scene::EntityId> light_entities_;
    std::vector<engine::Light> lights_;
    std::unique_ptr<detail::SceneSpatialIndex> spatial_index_;
    const scene::Scene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
    scene::SceneChangeCursor scene_change_cursor_;
    uint64_t assets_membership_revision_ = 0;
    uint64_t next_pick_id_ = 1;
    uint64_t generation_ = 1;
    uint64_t geometry_generation_ = 1;
    bool initialized_ = false;  // 首次 sync 全量，之后增量
};

}  // namespace mulan::view
