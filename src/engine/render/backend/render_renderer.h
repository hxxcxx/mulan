#pragma once

#include "render_compiler.h"
#include "render_surface_binding.h"
#include "../environment_map.h"
#include "../forward/edge_stage.h"
#include "../forward/face_stage.h"
#include "../frontend/render_request.h"
#include "../frontend/render_workload.h"
#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../render_resource_cache.h"
#include "../texture_cache.h"

#include <memory>
#include <string>

namespace mulan::engine {

class CommandList;
class RHIDevice;
class ViewCubeStage;

class RenderRenderer {
public:
    RenderRenderer();
    ~RenderRenderer();

    RenderRenderer(const RenderRenderer&) = delete;
    RenderRenderer& operator=(const RenderRenderer&) = delete;

    bool init(RHIDevice& device,
              LightEnvironment& lightEnv,
              TextureFormat colorFmt,
              TextureFormat depthFmt);
    void shutdown(RHIDevice& device);

    void enableIBL(RHIDevice& device, const std::string& hdrPath);
    void render(RHIDevice& device, const RenderSurfaceBinding& surface,
                const RenderRequest& request);

    RenderResourceCache& resources() { return *resources_; }
    bool isInitialized() const { return initialized_; }

private:
    void compile(const RenderRequest& request);
    CommandList* beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                            const RenderRequest& request);
    void executeStages(RenderFrame& frame);
    void endFrame(RHIDevice& device, const RenderSurfaceBinding& surface);

    std::unique_ptr<TextureCache> texture_cache_;
    std::unique_ptr<MaterialCache> material_cache_;
    std::unique_ptr<IBLPipeline> ibl_;
    std::unique_ptr<RenderResourceCache> resources_;

    RenderWorkload workload_;
    RenderCompiler compiler_;

    std::unique_ptr<FaceStage> face_stage_;
    std::unique_ptr<EdgeStage> edge_stage_;
    std::unique_ptr<ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

} // namespace mulan::engine
