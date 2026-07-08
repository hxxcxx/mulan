/**
 * @file render_contract.h
 * @brief 渲染端的契约（RenderContract）：渲染端对渲染对象的约束条件，保证渲染端的渲染对象在渲染管线中是合法的。
 * @author hxxcxx
 */
#pragma once

#include "render_object.h"
#include "../render_geometry.h"

namespace mulan::engine {

bool renderBucketUsesSurfacePass(RenderBucket bucket);
bool renderBucketUsesEdgePass(RenderBucket bucket);

bool renderBucketAcceptsTopology(RenderBucket bucket, graphics::PrimitiveTopology topology);
bool renderBucketAcceptsLayout(RenderBucket bucket, const graphics::VertexLayout& layout);

bool renderGeometryDescMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry);
bool renderGpuGeometryMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry,
                                    const GpuGeometry& gpuGeometry);

}  // namespace mulan::engine
