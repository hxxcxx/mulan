/**
 * @file render_compile_context.h
 * @brief RenderCompileContext 聚合 RenderCompiler 编译 GPU draw command 所需的 backend 资源。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

namespace mulan::engine {

class AssetGpuRegistry;
class MaterialCache;
class PipelineState;
class SurfacePipelineProvider;

struct RenderCompileContext {
    AssetGpuRegistry& assets;
    MaterialCache& materials;
    SurfacePipelineProvider* surfacePipelines = nullptr;
    PipelineState* edgePipeline = nullptr;
    PipelineState* highlightSurfacePipeline = nullptr;
    PipelineState* highlightSurfaceTangentPipeline = nullptr;
    PipelineState* highlightEdgePipeline = nullptr;
};

}  // namespace mulan::engine
