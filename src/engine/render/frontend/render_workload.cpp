#include "render_workload.h"

#include "render_contract.h"

namespace mulan::engine {
namespace {

bool bucketEnabled(const RenderWorkItem& item, const RenderOptions& options, RenderWorkloadStats& stats) {
    if (renderBucketIsOverlay(item.bucket)) {
        if (!options.showOverlays) {
            ++stats.skippedDisabledOverlayCount;
            return false;
        }
        return true;
    }

    switch (renderBucketPass(item.bucket)) {
    case RenderPassKind::Surface:
        if (!renderSurfacesEnabled(options)) {
            ++stats.skippedDisabledSurfaceCount;
            return false;
        }
        return true;
    case RenderPassKind::Edge:
        if (!renderEdgesEnabled(options) && !item.hovered) {
            ++stats.skippedDisabledEdgeCount;
            return false;
        }
        return true;
    case RenderPassKind::None: ++stats.skippedUnsupportedBucketCount; return false;
    }
    ++stats.skippedUnsupportedBucketCount;
    return false;
}

}  // namespace

void RenderWorkload::build(const RenderWorldSnapshot& snapshot, const RenderOptions& options) {
    clear();

    for (const auto& object : snapshot.objects()) {
        if (!object.desc.visible)
            continue;
        ++stats_.visibleObjectCount;

        for (const auto& drawable : object.desc.drawables) {
            RenderWorkItem item;
            item.bucket = drawable.bucket;
            item.geometry = drawable.geometry;
            item.material = drawable.material;
            item.worldTransform = object.desc.worldTransform;
            item.pickId = static_cast<uint32_t>(object.desc.externalId);
            item.sourceDrawableIndex = drawable.sourceDrawableIndex;
            item.selected = object.desc.selected;
            item.hovered = options.hasHoveredPickId && item.pickId == options.hoveredPickId;

            ++stats_.drawableCount;
            if (!bucketEnabled(item, options, stats_)) {
                continue;
            }

            switch (renderBucketPass(item.bucket)) {
            case RenderPassKind::Surface:
                surfaces_.push_back(item);
                ++stats_.surfaceItemCount;
                break;
            case RenderPassKind::Edge:
                edges_.push_back(item);
                ++stats_.edgeItemCount;
                break;
            case RenderPassKind::None: ++stats_.skippedUnsupportedBucketCount; break;
            }
        }
    }
}

void RenderWorkload::clear() {
    surfaces_.clear();
    edges_.clear();
    stats_.reset();
}

}  // namespace mulan::engine
