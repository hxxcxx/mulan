/**
 * @file render_visibility_index.h
 * @brief 管理场景对象的 Dynamic BVH、视锥查询和相机可见性缓存。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include "render_packet.h"
#include "../../frontend/render_view_desc.h"

#include <mulan/math/spatial/dynamic_bvh.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace mulan::engine::detail {

struct VisibilityQueryStats {
    bool frustumFailOpen = false;
    size_t sourceVisibleObjectCount = 0;
    size_t frustumVisibleObjectCount = 0;
    size_t culledObjectCount = 0;
    size_t uncullableObjectCount = 0;
    size_t bvhNodeBoundsTestCount = 0;
    size_t bvhLeafBoundsTestCount = 0;
};

class RenderVisibilityIndex {
public:
    void rebuild(std::span<const VisibilityItem> items, uint64_t sourceRevision);
    void clear();

    std::span<const RenderObjectId> resolve(const RenderViewDesc* view, bool sceneFrustumCulling);
    const VisibilityQueryStats& lastStats() const { return stats_; }

private:
    using ObjectIdHash = RenderHandleHash<RenderObjectIdTag>;
    using SpatialIndex = math::DynamicBVH<RenderObjectId, ObjectIdHash>;
    using ObjectSet = std::unordered_set<RenderObjectId, ObjectIdHash>;

    struct VisibilityCache {
        bool valid = false;
        math::Mat4 view;
        math::Mat4 projection;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failOpen = false;
        SpatialIndex::QueryStats queryStats;
        std::vector<RenderObjectId> ids;
    };

    SpatialIndex spatialIndex_;
    ObjectSet uncullableObjects_;
    std::vector<RenderObjectId> allVisibleIds_;
    VisibilityCache cache_;
    VisibilityQueryStats stats_;
    uint64_t sourceRevision_ = 0;
};

}  // namespace mulan::engine::detail
