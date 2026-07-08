#pragma once

#include "render_object.h"
#include "../render_geometry.h"

#include <cstdint>

namespace mulan::engine {

enum class RenderPassKind : uint8_t {
    None,
    Surface,
    Edge,
};

RenderPassKind renderBucketPass(RenderBucket bucket);
bool renderBucketIsOverlay(RenderBucket bucket);

bool renderBucketUsesSurfacePass(RenderBucket bucket);
bool renderBucketUsesEdgePass(RenderBucket bucket);

bool renderBucketAcceptsTopology(RenderBucket bucket, graphics::PrimitiveTopology topology);
bool renderBucketAcceptsLayout(RenderBucket bucket, const graphics::VertexLayout& layout);

bool renderGeometryDescMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry);
bool renderGpuGeometryMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry,
                                    const GpuGeometry& gpuGeometry);

}  // namespace mulan::engine
