#pragma once

#include "render_request.h"

#include <span>
#include <vector>

namespace mulan::engine {

struct RenderWorkItem {
    RenderBucket bucket = RenderBucket::Surface;
    GeometryHandle geometry;
    RenderMaterialHandle material;
    math::Mat4 worldTransform{1.0f};
    uint32_t pickId = 0;
    size_t sourceDrawableIndex = 0;
    bool selected = false;
};

class RenderWorkload {
public:
    void build(const RenderWorldSnapshot& snapshot, const RenderOptions& options);
    void clear();

    std::span<const RenderWorkItem> surfaces() const { return surfaces_; }
    std::span<const RenderWorkItem> edges() const { return edges_; }

private:
    std::vector<RenderWorkItem> surfaces_;
    std::vector<RenderWorkItem> edges_;
};

} // namespace mulan::engine
