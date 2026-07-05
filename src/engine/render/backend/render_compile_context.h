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
