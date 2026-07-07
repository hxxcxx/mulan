#include "render_workload.h"

namespace mulan::engine {

void RenderWorkload::build(const RenderWorldSnapshot& snapshot, const RenderOptions& options) {
    clear();

    for (const auto& object : snapshot.objects()) {
        if (!object.desc.visible)
            continue;

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

            switch (drawable.bucket) {
            case RenderBucket::Surface:
                if (renderSurfacesEnabled(options))
                    surfaces_.push_back(item);
                break;
            case RenderBucket::Edge:
                if (renderEdgesEnabled(options) || item.hovered)
                    edges_.push_back(item);
                break;
            case RenderBucket::Overlay:
                if (options.showOverlays)
                    edges_.push_back(item);
                break;
            case RenderBucket::Gizmo:
            case RenderBucket::Text: break;
            }
        }
    }
}

void RenderWorkload::clear() {
    surfaces_.clear();
    edges_.clear();
}

}  // namespace mulan::engine
