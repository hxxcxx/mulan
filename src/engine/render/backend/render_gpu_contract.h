/**
 * @file render_gpu_contract.h
 * @brief 渲染编译阶段使用的 CPU/GPU 几何契约校验。
 * @author hxxcxx
 * @date 2026-07-20
 */

#pragma once

#include "../frontend/render_object.h"

namespace mulan::engine {

struct GpuGeometry;

bool renderGeometryDescMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry);
bool renderGpuGeometryMatchesBucket(RenderBucket bucket, const RenderGeometryDesc& geometry,
                                    const GpuGeometry& gpuGeometry);

}  // namespace mulan::engine
