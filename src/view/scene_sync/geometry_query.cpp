/**
 * @file geometry_query.cpp
 * @brief GeometryQueryWorld 实现 —— BVH 宽阶段裁剪后委托资产 picking 子系统。
 * @author hxxcxx
 * @date 2026-07-11 (原始) / 2026-07-15 (场景级 BVH 宽阶段)
 */
#include "scene_sync/geometry_query.h"

#include "detail/asset_picking.h"
#include "detail/picking_types.h"
#include "detail/scene_spatial_index.h"

#include <mulan/asset/asset_library.h>
#include <mulan/math/algo/intersect.h>

#include <algorithm>
#include <utility>

namespace mulan::view {

using detail::appendGeometryAssetPickCandidates;
using detail::expandedBounds;
using detail::MeshPickResult;
using detail::pickGeometryAsset;
using detail::pickResultFromMeshHit;

GeometryQueryWorld::GeometryQueryWorld(const RenderScene& scene) : scene_(&scene), assets_(scene.assets_) {
}

void GeometryQueryWorld::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld,
                                               std::vector<RenderScene::PickResult>& out, PickQueryStats* stats) const {
    out.clear();
    if (stats) {
        *stats = {};
    }
    if (!scene_ || !assets_) {
        return;
    }

    const double tolerance = std::max(0.0, lineToleranceWorld);
    std::vector<scene::EntityId> proxyCandidates;
    detail::SpatialIndexQueryStats spatialStats;
    scene_->spatial_index_->queryRay(ray, tolerance, proxyCandidates, &spatialStats);
    if (stats) {
        stats->visibleProxyCount = spatialStats.indexedProxyCount + spatialStats.fallbackProxyCount;
        stats->indexedProxyCount = spatialStats.indexedProxyCount;
        stats->fallbackProxyCount = spatialStats.fallbackProxyCount;
        stats->nodeBoundsTestCount = spatialStats.nodeBoundsTestCount;
        stats->leafBoundsTestCount = spatialStats.leafBoundsTestCount;
        stats->candidateProxyCount = spatialStats.candidateProxyCount;
    }

    std::vector<MeshPickResult> meshHits;
    for (scene::EntityId id : proxyCandidates) {
        const SceneProxy* proxy = scene_->proxy(id);
        if (!proxy || !proxy->visible) {
            continue;
        }

        const asset::Asset* geometryAsset = assets_->asset(proxy->geometry);
        if (!geometryAsset) {
            continue;
        }

        if (stats) {
            ++stats->exactAssetTestCount;
        }
        meshHits.clear();
        appendGeometryAssetPickCandidates(ray, *geometryAsset, proxy->worldTransform, tolerance, meshHits);
        for (const MeshPickResult& meshHit : meshHits) {
            if (!meshHit.distance) {
                continue;
            }
            out.push_back(pickResultFromMeshHit(id, *proxy, meshHit, tolerance));
        }
    }
}

std::optional<RenderScene::PickResult> GeometryQueryWorld::pick(const math::Ray3& ray, double lineToleranceWorld,
                                                                PickQueryStats* stats) const {
    if (stats) {
        *stats = {};
    }
    if (!scene_) {
        return std::nullopt;
    }

    const double tolerance = std::max(0.0, lineToleranceWorld);
    std::vector<scene::EntityId> proxyCandidates;
    detail::SpatialIndexQueryStats spatialStats;
    scene_->spatial_index_->queryRay(ray, tolerance, proxyCandidates, &spatialStats);
    if (stats) {
        stats->visibleProxyCount = spatialStats.indexedProxyCount + spatialStats.fallbackProxyCount;
        stats->indexedProxyCount = spatialStats.indexedProxyCount;
        stats->fallbackProxyCount = spatialStats.fallbackProxyCount;
        stats->nodeBoundsTestCount = spatialStats.nodeBoundsTestCount;
        stats->leafBoundsTestCount = spatialStats.leafBoundsTestCount;
        stats->candidateProxyCount = spatialStats.candidateProxyCount;
    }

    std::optional<RenderScene::PickResult> best;
    for (scene::EntityId id : proxyCandidates) {
        const SceneProxy* proxy = scene_->proxy(id);
        if (!proxy || !proxy->visible) {
            continue;
        }

        std::optional<RenderScene::PickResult> candidate;
        if (detail::SceneSpatialIndex::isIndexableBounds(proxy->worldBounds)) {
            const math::AABB3 pickBounds = expandedBounds(proxy->worldBounds, tolerance);
            const auto boundsHit = math::intersect(ray, pickBounds);
            if (!boundsHit.hit) {
                // BVH 叶条目使用相同扩展规则；该分支只防止数值或未来实现偏差。
                continue;
            }
            candidate = RenderScene::PickResult{
                .entity = id,
                .pickId = proxy->pickId,
                .distance = boundsHit.t,
                .kind = RenderScene::PickHitKind::Object,
                .toleranceWorld = tolerance,
            };
        }

        if (assets_) {
            const asset::Asset* geometryAsset = assets_->asset(proxy->geometry);
            if (geometryAsset) {
                if (stats) {
                    ++stats->exactAssetTestCount;
                }
                const MeshPickResult meshHit = pickGeometryAsset(ray, *geometryAsset, proxy->worldTransform, tolerance);
                if (meshHit.tested) {
                    if (!meshHit.distance) {
                        continue;
                    }
                    candidate = pickResultFromMeshHit(id, *proxy, meshHit, tolerance);
                    candidate->toleranceWorld = meshHit.toleranceWorld;
                }
            }
        }

        // 空或非有限 bounds 没有可用的 Object 距离；只有资产级精确命中才能成为结果。
        if (candidate && (!best || candidate->distance < best->distance)) {
            best = std::move(candidate);
        }
    }
    return best;
}

}  // namespace mulan::view
