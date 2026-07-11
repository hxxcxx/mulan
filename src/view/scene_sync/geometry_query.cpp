/**
 * @file geometry_query.cpp
 * @brief GeometryQueryWorld 实现 —— 遍历场景代理，委托 picking 子系统。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include <mulan/view/scene_sync/geometry_query.h>

#include "asset_picking.h"
#include "picking_types.h"

#include <mulan/asset/asset_library.h>
#include <mulan/math/algo/intersect.h>

namespace mulan::view {

using detail::appendGeometryAssetPickCandidates;
using detail::expandedBounds;
using detail::MeshPickResult;
using detail::pickGeometryAsset;
using detail::pickResultFromMeshHit;

GeometryQueryWorld::GeometryQueryWorld(const RenderScene& scene) : scene_(&scene), assets_(scene.assets_) {
}

void GeometryQueryWorld::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld,
                                               std::vector<RenderScene::PickResult>& out) const {
    out.clear();
    if (!scene_ || !assets_) {
        return;
    }

    std::vector<MeshPickResult> meshHits;
    for (const auto& [id, proxy] : scene_->proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const math::AABB3 pickBounds = expandedBounds(proxy.worldBounds, lineToleranceWorld);
        const auto boundsHit = math::intersect(ray, pickBounds);
        if (!boundsHit.hit) {
            continue;
        }

        const asset::Asset* geometryAsset = assets_->asset(proxy.geometry);
        if (!geometryAsset) {
            continue;
        }

        meshHits.clear();
        appendGeometryAssetPickCandidates(ray, *geometryAsset, proxy.worldTransform, lineToleranceWorld, meshHits);
        for (const MeshPickResult& meshHit : meshHits) {
            if (!meshHit.distance) {
                continue;
            }
            out.push_back(pickResultFromMeshHit(id, proxy, meshHit, lineToleranceWorld));
        }
    }
}

std::optional<RenderScene::PickResult> GeometryQueryWorld::pick(const math::Ray3& ray,
                                                                double lineToleranceWorld) const {
    if (!scene_) {
        return std::nullopt;
    }

    std::optional<RenderScene::PickResult> best;
    for (const auto& [id, proxy] : scene_->proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const math::AABB3 pickBounds = expandedBounds(proxy.worldBounds, lineToleranceWorld);
        const auto boundsHit = math::intersect(ray, pickBounds);
        if (!boundsHit.hit) {
            continue;
        }

        RenderScene::PickResult candidate{
            .entity = id,
            .pickId = engine::PickId::fromValue(proxy.entity.index()),
            .distance = boundsHit.t,
            .kind = RenderScene::PickHitKind::Object,
            .toleranceWorld = lineToleranceWorld,
        };
        if (assets_) {
            const asset::Asset* geometryAsset = assets_->asset(proxy.geometry);
            if (geometryAsset) {
                const MeshPickResult meshHit =
                        pickGeometryAsset(ray, *geometryAsset, proxy.worldTransform, lineToleranceWorld);
                if (meshHit.tested) {
                    if (!meshHit.distance) {
                        continue;
                    }
                    candidate = pickResultFromMeshHit(id, proxy, meshHit, lineToleranceWorld);
                    candidate.toleranceWorld = meshHit.toleranceWorld;
                }
            }
        }

        if (!best || candidate.distance < best->distance) {
            best = candidate;
        }
    }
    return best;
}

}  // namespace mulan::view
