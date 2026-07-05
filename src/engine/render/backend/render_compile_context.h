/**
 * @file render_compile_context.h
 * @brief RenderCompileContext 聚合 RenderCompiler 编译 GPU draw command 所需的 backend 资源。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

namespace mulan::engine {

class MaterialCache;
class PipelineState;
class RenderResourceCache;
class TextureCache;

struct RenderCompileContext {
    RenderResourceCache& resources;
    TextureCache& textures;
    MaterialCache& materials;
    PipelineState* surfacePipeline = nullptr;
    PipelineState* edgePipeline = nullptr;
};

} // namespace mulan::engine
