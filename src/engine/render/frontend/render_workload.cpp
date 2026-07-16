#include "render_workload.h"

#include "render_contract.h"

namespace mulan::engine {
namespace {

bool targetRoleMatches(const SelectionVisualTarget& target, SelectionVisualRole role) {
    return target.role == role;
}

bool targetMatchesDrawable(const SelectionVisualTarget& target, RenderBucket bucket, PickId pickId,
                           size_t sourceDrawableIndex) {
    if (!target.valid() || target.pickId != pickId) {
        return false;
    }

    if (target.wholeEntity()) {
        return true;
    }

    switch (target.domain) {
    case SelectionVisualDomain::CurveElement:
    case SelectionVisualDomain::CurveSegment:
    case SelectionVisualDomain::CurveVertex:
        if (renderBucketPass(bucket) != RenderPassKind::Edge) {
            return false;
        }
        if (target.hasSourceDrawableIndex) {
            return sourceDrawableIndex == target.sourceDrawableIndex;
        }
        return target.hasPrimitiveIndex && sourceDrawableIndex == target.primitiveIndex;
    case SelectionVisualDomain::MeshFace:
    case SelectionVisualDomain::MeshEdge:
    case SelectionVisualDomain::MeshVertex:
    case SelectionVisualDomain::SurfaceFace:
    case SelectionVisualDomain::SurfaceEdge:
    case SelectionVisualDomain::SurfaceVertex:
    case SelectionVisualDomain::SolidFace:
    case SelectionVisualDomain::SolidEdge:
    case SelectionVisualDomain::SolidVertex: return true;
    case SelectionVisualDomain::ControlPoint:
    case SelectionVisualDomain::Grip:
    case SelectionVisualDomain::Entity: return false;
    }
    return false;
}

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
        if (!renderEdgesEnabled(options)) {
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

RenderVisualMatch renderVisualMatch(RenderBucket bucket, PickId pickId, size_t sourceDrawableIndex,
                                    bool defaultSelected, const RenderOptions& options) {
    RenderVisualMatch match;
    if (options.selectionVisuals.active()) {
        for (const SelectionVisualTarget& target : options.selectionVisuals.targets()) {
            if (!targetMatchesDrawable(target, bucket, pickId, sourceDrawableIndex)) {
                continue;
            }
            match.selected = match.selected || targetRoleMatches(target, SelectionVisualRole::Selected);
            match.hovered = match.hovered || targetRoleMatches(target, SelectionVisualRole::Hovered);
        }
        return match;
    }

    match.selected = defaultSelected;
    match.hovered = options.hoveredPickId.valid() && pickId == options.hoveredPickId;
    return match;
}

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
            item.pickId = object.desc.pickId;
            item.sourceDrawableIndex = drawable.sourceDrawableIndex;
            item.selected = object.desc.selected;

            const RenderVisualMatch visualMatch =
                    renderVisualMatch(item.bucket, item.pickId, item.sourceDrawableIndex, item.selected, options);
            item.selected = visualMatch.selected;
            item.hovered = visualMatch.hovered;

            ++stats_.drawableCount;
            const RenderPassKind pass = renderBucketPass(item.bucket);
            if (!renderBucketIsOverlay(item.bucket)) {
                if (item.selected && pass == RenderPassKind::Surface && renderSurfacesEnabled(options)) {
                    highlight_surfaces_.push_back(item);
                    ++stats_.highlightSurfaceItemCount;
                }
                if ((item.selected || item.hovered) && pass == RenderPassKind::Edge) {
                    highlight_edges_.push_back(item);
                    ++stats_.highlightEdgeItemCount;
                }
            }

            if (!bucketEnabled(item, options, stats_)) {
                continue;
            }

            switch (pass) {
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
    highlight_surfaces_.clear();
    highlight_edges_.clear();
    stats_.reset();
}

}  // namespace mulan::engine
