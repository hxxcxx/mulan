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
#include "../forward/highlight_stage.h"
#include "../frontend/render_request.h"
#include "../frontend/render_resource_prepare.h"
#include "../frontend/render_workload.h"
#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../asset_gpu_registry.h"
#include "../draw/geometry_draw_shared_resources.h"

#include <mulan/core/result/error.h>

#include <memory>
#include <string>
#include <vector>

namespace mulan::engine {

class CommandList;
class RHIDevice;
class TextStage;
class ViewCubeStage;

class RenderRenderer {
public:
    RenderRenderer();
    ~RenderRenderer();

    RenderRenderer(const RenderRenderer&) = delete;
    RenderRenderer& operator=(const RenderRenderer&) = delete;

    bool init(RHIDevice& device, LightEnvironment& lightEnv, TextureFormat colorFmt, TextureFormat depthFmt,
              uint32_t sampleCount);
    void shutdown(RHIDevice& device);

    void enableIBL(RHIDevice& device, const std::string& hdrPath);
    /// 上传跨帧持久资源；只有整个批次完成后才返回成功，供上层生成可靠 ACK。
    ResultVoid preparePersistentResources(RHIDevice& device, const RenderResourcePrepareList& prepare);
    ResultVoid render(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    /// 释放全部资产派生 GPU 资源（资产域切换时由 view RenderSession 触发）。
    void clearAssetResources(RHIDevice& device);

    bool isInitialized() const { return initialized_; }
    const RenderWorkloadStats& lastWorkloadStats() const { return scene_workload_.lastStats(); }
    const RenderCompilerStats& lastCompilerStats() const { return scene_compiler_.lastStats(); }

private:
    bool validateOutput(const RenderSurfaceBinding& surface, const RenderRequest& request) const;
    void clearCompiledCommands();
    ResultVoid prepareFrameResources(const RenderRequest& request);
    ResultVoid compile(const RenderRequest& request);
    DrawExecutionContext buildDrawContext(CommandList& cmd, const RenderFrame& frame) const;
    Result<CommandList*> beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                    const RenderRequest& request);
    void executeStages(RenderFrame& frame, const TextDrawList& requestTextDraws);
    ResultVoid endFrame(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    std::unique_ptr<MaterialCache> material_cache_;
    std::unique_ptr<IBLPipeline> ibl_;
    std::unique_ptr<AssetGpuRegistry> asset_gpu_registry_;
    std::unique_ptr<GeometryDrawSharedResources> geometry_resources_;

    RenderWorkload scene_workload_;
    RenderWorkload overlay_workload_;
    RenderCompiler scene_compiler_;
    RenderCompiler overlay_compiler_;
    // Stage 持有 span，合并命令必须由 Renderer 保持到本帧执行结束。
    std::vector<MeshDrawCommand> surface_commands_;
    std::vector<MeshDrawCommand> edge_commands_;
    std::vector<MeshDrawCommand> highlight_surface_commands_;
    std::vector<MeshDrawCommand> highlight_edge_commands_;

    std::unique_ptr<FaceStage> face_stage_;
    std::unique_ptr<EdgeStage> edge_stage_;
    std::unique_ptr<HighlightStage> highlight_stage_;
    std::unique_ptr<TextStage> text_stage_;
    std::unique_ptr<ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

}  // namespace mulan::engine
