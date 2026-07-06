/**
 * @file render_renderer.h
 * @brief RenderRenderer 是 engine backend 的一帧渲染编排入口。
 * @author hxxcxx
 * @date 2026-07-05
 */

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
#include "../asset_gpu_registry.h"

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

    bool init(RHIDevice& device, LightEnvironment& lightEnv, TextureFormat colorFmt, TextureFormat depthFmt);
    void shutdown(RHIDevice& device);

    void enableIBL(RHIDevice& device, const std::string& hdrPath);
    void render(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    /// 释放全部资产派生 GPU 资源（文档切换时由 Renderer::setScene 触发）。
    void clearAssetResources(RHIDevice& device);

    bool isInitialized() const { return initialized_; }

private:
    bool validateOutput(const RenderSurfaceBinding& surface, const RenderRequest& request) const;
    void clearCompiledCommands();
    void prepareResources(const RenderRequest& request);
    void compile(const RenderRequest& request);
    CommandList* beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);
    void executeStages(RenderFrame& frame);
    void endFrame(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    std::unique_ptr<MaterialCache> material_cache_;
    std::unique_ptr<IBLPipeline> ibl_;
    std::unique_ptr<AssetGpuRegistry> asset_gpu_registry_;

    RenderWorkload workload_;
    RenderCompiler compiler_;

    std::unique_ptr<FaceStage> face_stage_;
    std::unique_ptr<EdgeStage> edge_stage_;
    std::unique_ptr<ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

}  // namespace mulan::engine
