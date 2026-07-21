#include "render_visibility_index.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <optional>

namespace mulan::engine::detail {
namespace {

using SpatialIndex = math::DynamicBVH<RenderObjectId, RenderHandleHash<RenderObjectIdTag>>;

bool objectIdLess(RenderObjectId lhs, RenderObjectId rhs) noexcept {
    return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
}

bool matricesEqual(const math::Mat4& lhs, const math::Mat4& rhs) noexcept {
    for (int index = 0; index < 16; ++index) {
        if (lhs.data()[index] != rhs.data()[index])
            return false;
    }
    return true;
}

bool indexableBounds(const math::AABB3& bounds) {
    return SpatialIndex::isValidBounds(bounds) && !bounds.isEmpty();
}

}  // namespace

bool RenderVisibilityIndex::sync(const RenderWorldSnapshot& snapshot) {
    MULAN_PROFILE_ZONE();

    const RenderWorldVersion version = snapshot.version();
    if (hasSource_ && sourceWorld_ == version.world && sourceRevision_ == version.spatialRevision)
        return false;

    spatialIndex_.clear();
    uncullableObjects_.clear();
    allVisibleIds_.clear();
    spatialIndex_.reserve(snapshot.objects().size());
    allVisibleIds_.reserve(snapshot.objects().size());

    for (const RenderObjectRecord& object : snapshot.objects()) {
        if (!object.desc.visible)
            continue;
        allVisibleIds_.push_back(object.id);
        if (!indexableBounds(object.desc.worldBounds) || !spatialIndex_.insert(object.id, object.desc.worldBounds))
            uncullableObjects_.insert(object.id);
    }
    std::sort(allVisibleIds_.begin(), allVisibleIds_.end(), objectIdLess);
    cache_.valid = false;
    cache_.ids.clear();
    sourceWorld_ = version.world;
    sourceRevision_ = version.spatialRevision;
    hasSource_ = true;
    return true;
}

void RenderVisibilityIndex::clear() {
    spatialIndex_.clear();
    uncullableObjects_.clear();
    allVisibleIds_.clear();
    cache_ = {};
    stats_ = {};
    sourceWorld_ = 0;
    sourceRevision_ = 0;
    hasSource_ = false;
}

std::span<const RenderObjectId> RenderVisibilityIndex::resolve(const RenderViewDesc* view, bool sceneFrustumCulling) {
    MULAN_PROFILE_ZONE();

    stats_ = {};
    stats_.sourceVisibleObjectCount = allVisibleIds_.size();
    stats_.uncullableObjectCount = uncullableObjects_.size();
    if (!sceneFrustumCulling) {
        stats_.frustumVisibleObjectCount = allVisibleIds_.size();
        return allVisibleIds_;
    }

    if (view && cache_.valid && cache_.width == view->width && cache_.height == view->height &&
        matricesEqual(cache_.view, view->viewMatrix) && matricesEqual(cache_.projection, view->projectionMatrix)) {
        stats_.frustumFailOpen = cache_.failOpen;
        stats_.bvhNodeBoundsTestCount = cache_.queryStats.nodeBoundsTestCount;
        stats_.bvhLeafBoundsTestCount = cache_.queryStats.leafBoundsTestCount;
        stats_.frustumVisibleObjectCount = cache_.ids.size();
        stats_.culledObjectCount = allVisibleIds_.size() - cache_.ids.size();
        return cache_.ids;
    }

    std::vector<RenderObjectId> ids;
    SpatialIndex::QueryStats queryStats;
    bool failOpen = view == nullptr;
    std::optional<math::Frustum3> frustum;
    if (view)
        frustum = math::Frustum3::tryFromViewProjection(view->projectionMatrix * view->viewMatrix);
    if (!frustum) {
        failOpen = true;
        ids = allVisibleIds_;
    } else {
        ids.reserve(spatialIndex_.size() + uncullableObjects_.size());
        spatialIndex_.queryFrustum(
                *frustum, math::defaultTolerance().lengthEps,
                [&](RenderObjectId id) {
                    ids.push_back(id);
                    return true;
                },
                &queryStats);
        ids.insert(ids.end(), uncullableObjects_.begin(), uncullableObjects_.end());
        std::sort(ids.begin(), ids.end(), objectIdLess);
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }

    stats_.frustumFailOpen = failOpen;
    stats_.bvhNodeBoundsTestCount = queryStats.nodeBoundsTestCount;
    stats_.bvhLeafBoundsTestCount = queryStats.leafBoundsTestCount;
    stats_.frustumVisibleObjectCount = ids.size();
    stats_.culledObjectCount = allVisibleIds_.size() - ids.size();
    if (!view) {
        // 没有相机时按 fail-open 返回稳定的全量列表，但不污染相机缓存。
        return allVisibleIds_;
    }

    cache_.valid = true;
    cache_.view = view->viewMatrix;
    cache_.projection = view->projectionMatrix;
    cache_.width = view->width;
    cache_.height = view->height;
    cache_.failOpen = failOpen;
    cache_.queryStats = queryStats;
    cache_.ids = std::move(ids);
    return cache_.ids;
}

}  // namespace mulan::engine::detail
