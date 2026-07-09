/**
 * @file render_workload.h
 * @brief RenderWorkload 将 RenderWorldSnapshot 分类为 backend 可编译的渲染 bucket。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_request.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mulan::engine {

struct RenderWorkItem {
    RenderBucket bucket = RenderBucket::Surface;
    GeometryHandle geometry;
    RenderMaterialHandle material;
    math::Mat4 worldTransform{ 1.0f };
    PickId pickId;
    size_t sourceDrawableIndex = 0;
    bool selected = false;
    bool hovered = false;
};

struct RenderWorkloadStats {
    size_t visibleObjectCount = 0;
    size_t drawableCount = 0;
    size_t surfaceItemCount = 0;
    size_t edgeItemCount = 0;
    size_t highlightSurfaceItemCount = 0;
    size_t highlightEdgeItemCount = 0;
    size_t skippedDisabledSurfaceCount = 0;
    size_t skippedDisabledEdgeCount = 0;
    size_t skippedDisabledOverlayCount = 0;
    size_t skippedUnsupportedBucketCount = 0;

    void reset() { *this = {}; }
};

class RenderWorkload {
public:
    void build(const RenderWorldSnapshot& snapshot, const RenderOptions& options);
    void clear();

    std::span<const RenderWorkItem> surfaces() const { return surfaces_; }
    std::span<const RenderWorkItem> edges() const { return edges_; }
    std::span<const RenderWorkItem> highlightSurfaces() const { return highlight_surfaces_; }
    std::span<const RenderWorkItem> highlightEdges() const { return highlight_edges_; }
    const RenderWorkloadStats& lastStats() const { return stats_; }

private:
    std::vector<RenderWorkItem> surfaces_;
    std::vector<RenderWorkItem> edges_;
    std::vector<RenderWorkItem> highlight_surfaces_;
    std::vector<RenderWorkItem> highlight_edges_;
    RenderWorkloadStats stats_;
};

}  // namespace mulan::engine
