#include "render_contract.h"

namespace mulan::engine {
namespace {

bool layoutEquals(const graphics::VertexLayout& lhs, const graphics::VertexLayout& rhs) {
    if (lhs.stride() != rhs.stride() || lhs.attrCount() != rhs.attrCount() || lhs.bufferCount() != rhs.bufferCount()) {
        return false;
    }

    for (uint8_t i = 0; i < lhs.attrCount(); ++i) {
        if (!(lhs[i] == rhs[i])) {
            return false;
        }
    }
    return true;
}

bool isSurfaceLayout(const graphics::VertexLayout& layout) {
    return layoutEquals(layout, graphics::layouts::surface()) || layoutEquals(layout, graphics::layouts::pbr());
}

bool isEdgeLayout(const graphics::VertexLayout& layout) {
    return layoutEquals(layout, graphics::layouts::surface());
}

}  // namespace

bool renderBucketUsesSurfacePass(RenderBucket bucket) {
    switch (bucket) {
    case RenderBucket::Surface:
    case RenderBucket::OverlaySurface: return true;
    case RenderBucket::Edge:
    case RenderBucket::OverlayEdge:
    case RenderBucket::Gizmo:
    case RenderBucket::Text: return false;
    }
    return false;
}

bool renderBucketUsesEdgePass(RenderBucket bucket) {
    switch (bucket) {
    case RenderBucket::Edge:
    case RenderBucket::OverlayEdge: return true;
    case RenderBucket::Surface:
    case RenderBucket::OverlaySurface:
    case RenderBucket::Gizmo:
    case RenderBucket::Text: return false;
    }
    return false;
}

bool renderBucketAcceptsTopology(RenderBucket bucket, graphics::PrimitiveTopology topology) {
    if (renderBucketUsesSurfacePass(bucket)) {
        return topology == graphics::PrimitiveTopology::TriangleList;
    }
    if (renderBucketUsesEdgePass(bucket)) {
        return topology == graphics::PrimitiveTopology::LineList;
    }
    return false;
}

bool renderBucketAcceptsLayout(RenderBucket bucket, const graphics::VertexLayout& layout) {
    if (renderBucketUsesSurfacePass(bucket)) {
        return isSurfaceLayout(layout);
    }
    if (renderBucketUsesEdgePass(bucket)) {
        return isEdgeLayout(layout);
    }
    return false;
}

bool renderGeometryDescMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry) {
    if (geometry.empty) {
        return false;
    }
    return renderBucketAcceptsTopology(bucket, geometry.topology) &&
           renderBucketAcceptsLayout(bucket, geometry.vertexLayout);
}

bool renderGpuGeometryMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry,
                                    const GpuGeometry& gpuGeometry) {
    if (!renderGeometryDescMatchesBucket(bucket, geometry)) {
        return false;
    }
    return renderBucketAcceptsLayout(bucket, gpuGeometry.layout);
}

}  // namespace mulan::engine
