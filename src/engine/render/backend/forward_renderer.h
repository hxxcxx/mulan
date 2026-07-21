/**
 * @file forward_renderer.h
 * @brief ForwardRenderer 编译并执行单个视图的固定 Forward 渲染流程。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_compiler.h"
#include "render_output.h"
#include "../environment_map.h"
#include "../forward/edge_stage.h"
#include "../forward/face_stage.h"
#include "../forward/highlight_stage.h"
#include "../frontend/render_request.h"
#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../asset_gpu_registry.h"
#include "../draw/draw_fallback_resources.h"
#include "../draw/geometry_draw_shared_resources.h"
#include "../device_resource_service.h"
#include "../frame/render_target_info.h"

#include <mulan/core/result/error.h>

#include <memory>
#include <string>

namespace mulan::engine {

class CommandList;
class RHIDevice;
class TextStage;
class ViewCubeStage;

class ForwardRenderer {
public:
    ForwardRenderer();
    ~ForwardRenderer();

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    ResultVoid init(RHIDevice& device, DeviceResourceService& resources, const RenderTargetInfo& target);
    void shutdown(RHIDevice& device);

    void enableIBL(RHIDevice& device, const std::string& hdrPath);
    ResultVoid render(RHIDevice& device, const RenderOutput& output, const RenderRequest& request,
                      const LightEnvironment& lightEnvironment);

    bool isInitialized() const { return initialized_; }

private:
    void clearCompiledCommands();
    void publishCompiledCommands();
    ResultVoid compile(const RenderRequest& request);
    DrawExecutionContext buildDrawContext(CommandList& cmd, const RenderFrame& frame) const;
    Result<CommandList*> beginFrame(RHIDevice& device, const RenderOutput& output, const RenderViewDesc& view);
    void executeStages(RenderFrame& frame);
    ResultVoid endFrame(RHIDevice& device, const RenderOutput& output);

    MaterialCache* material_cache_ = nullptr;
    std::unique_ptr<IBLPipeline> ibl_;
    AssetGpuRegistry* asset_gpu_registry_ = nullptr;
    GeometryDrawSharedResources* geometry_resources_ = nullptr;
    DrawFallbackResources* fallback_resources_ = nullptr;

    RenderCompiler scene_compiler_;
    RenderCompiler overlay_compiler_;

    std::unique_ptr<FaceStage> face_stage_;
    std::unique_ptr<EdgeStage> edge_stage_;
    std::unique_ptr<HighlightStage> highlight_stage_;
    // DeviceResourceService 持有共享 TextStage；Renderer 只在串行执行期间借用。
    TextStage* text_stage_ = nullptr;
    std::unique_ptr<ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

}  // namespace mulan::engine
