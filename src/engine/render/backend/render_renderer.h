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
#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../asset_gpu_registry.h"
#include "../draw/geometry_draw_shared_resources.h"
#include "../device_resource_service.h"

#include <mulan/core/result/error.h>

#include <memory>
#include <cstdint>
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

    bool init(RHIDevice& device, DeviceResourceService& resources, LightEnvironment& lightEnv, TextureFormat colorFmt,
              TextureFormat depthFmt, uint32_t sampleCount);
    void shutdown(RHIDevice& device);

    void enableIBL(RHIDevice& device, const std::string& hdrPath);
    /// 上传跨帧持久资源；只有整个批次完成后才返回成功，供上层生成可靠 ACK。
    ResultVoid preparePersistentResources(DeviceResourceClientId client, const RenderResourcePrepareList& prepare);
    ResultVoid render(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    bool isInitialized() const { return initialized_; }
    const RenderWorkloadStats& lastWorkloadStats() const { return scene_compiler_.lastWorkloadStats(); }
    const RenderCompilerStats& lastCompilerStats() const { return scene_compiler_.lastStats(); }
    const RenderPacketCacheStats& lastPacketCacheStats() const { return scene_compiler_.lastPacketCacheStats(); }

private:
    bool validateOutput(const RenderSurfaceBinding& surface, const RenderRequest& request) const;
    void clearCompiledCommands();
    ResultVoid compile(const RenderRequest& request);
    DrawExecutionContext buildDrawContext(CommandList& cmd, const RenderFrame& frame) const;
    Result<CommandList*> beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                    const RenderRequest& request);
    void executeStages(RenderFrame& frame, const TextDrawList& requestTextDraws);
    ResultVoid endFrame(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request);

    DeviceResourceService* device_resources_ = nullptr;
    MaterialCache* material_cache_ = nullptr;
    std::unique_ptr<IBLPipeline> ibl_;
    AssetGpuRegistry* asset_gpu_registry_ = nullptr;
    GeometryDrawSharedResources* geometry_resources_ = nullptr;
    LightEnvironment* light_environment_ = nullptr;

    RenderCompiler scene_compiler_;
    RenderCompiler overlay_compiler_;
    // Stage 持有 span，合并命令必须由 Renderer 保持到本帧执行结束。
    std::vector<MeshDrawCommand> surface_commands_;
    std::vector<MeshDrawCommand> edge_commands_;
    std::vector<MeshDrawCommand> highlight_surface_commands_;
    std::vector<MeshDrawCommand> highlight_edge_commands_;
    uint64_t merged_scene_command_revision_ = 0;
    uint64_t merged_overlay_command_revision_ = 0;
    bool merged_commands_valid_ = false;

    std::unique_ptr<FaceStage> face_stage_;
    std::unique_ptr<EdgeStage> edge_stage_;
    std::unique_ptr<HighlightStage> highlight_stage_;
    // DeviceResourceService 持有共享 TextStage；Renderer 只在串行执行期间借用。
    TextStage* text_stage_ = nullptr;
    std::unique_ptr<ViewCubeStage> view_cube_stage_;

    bool initialized_ = false;
};

}  // namespace mulan::engine
