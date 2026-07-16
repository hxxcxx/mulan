#include "render_renderer.h"

#include "../frame/render_frame.h"
#include "../overlay/view_cube_stage.h"
#include "../text/text_stage.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../rhi/engine_error_code.h"
#include "../../rhi/render_target.h"
#include "../../rhi/render_types.h"
#include "../../rhi/swap_chain.h"

#include <mulan/core/log/log.h>

#include <cstdio>
#include <optional>
#include <span>

namespace mulan::engine {
RenderRenderer::RenderRenderer() = default;
RenderRenderer::~RenderRenderer() = default;

ResultVoid RenderRenderer::init(RHIDevice& device, DeviceResourceService& resources, LightEnvironment& lightEnv,
                                TextureFormat colorFmt, TextureFormat depthFmt, uint32_t sampleCount) {
    if (initialized_)
        return {};

    device_resources_ = &resources;
    material_cache_ = &resources.materials();
    asset_gpu_registry_ = &resources.assets();
    geometry_resources_ = &resources.geometryDrawResources();
    light_environment_ = &lightEnv;

    RenderTargetInfo targetInfo;
    targetInfo.colorFormat = colorFmt;
    targetInfo.depthFormat = depthFmt;
    targetInfo.hasDepth = true;
    targetInfo.sampleCount = sampleCount;

    face_stage_ = std::make_unique<FaceStage>(device, *geometry_resources_, resources.pipelines());
    if (auto initialized = face_stage_->init(device, targetInfo); !initialized)
        return std::unexpected(initialized.error());

    edge_stage_ = std::make_unique<EdgeStage>(device, *geometry_resources_, resources.pipelines());
    if (auto initialized = edge_stage_->init(device, targetInfo); !initialized)
        return std::unexpected(initialized.error());

    highlight_stage_ = std::make_unique<HighlightStage>(device, *geometry_resources_, resources.pipelines());
    if (auto initialized = highlight_stage_->init(device, targetInfo); !initialized)
        return std::unexpected(initialized.error());

    view_cube_stage_ = std::make_unique<ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, targetInfo)) {
        LOG_WARN("[RenderRenderer] ViewCube stage initialization failed; continuing without it");
        view_cube_stage_.reset();
    }

    text_stage_ = resources.acquireTextStage(targetInfo);
    if (!text_stage_) {
        LOG_WARN("[RenderRenderer] Text stage initialization failed; continuing without it");
    }

    initialized_ = true;
    LOG_INFO("[RenderRenderer] Initialized: colorFormat={}, depthFormat={}, sampleCount={}, viewCube={}, text={}",
             static_cast<int>(colorFmt), static_cast<int>(depthFmt), sampleCount, view_cube_stage_ != nullptr,
             text_stage_ != nullptr);
    return {};
}

void RenderRenderer::shutdown(RHIDevice& device) {
    // 即使 init() 未能完成（initialized_ 仍为 false），也可能已分配部分 RHI 资源
    //（如 GeometryDrawSharedResources 的 UBO）。因此 shutdown 必须无条件执行清理，
    // 否则这些资源会在 device 析构时触发 assertNoLiveResources 断言。
    clearCompiledCommands();

    // TextStage 属于 DeviceResourceService，不随单个 Surface/Renderer 退役。
    text_stage_ = nullptr;
    auto releaseResources = [viewCubeStage = std::move(view_cube_stage_), highlightStage = std::move(highlight_stage_),
                             edgeStage = std::move(edge_stage_), faceStage = std::move(face_stage_),
                             ibl = std::move(ibl_)]() mutable {
        viewCubeStage.reset();
        highlightStage.reset();
        edgeStage.reset();
        faceStage.reset();
        ibl.reset();
    };

    const SubmissionToken token = device.lastSubmissionToken();
    if (token) {
        auto retireResult = device.retire(token, std::move(releaseResources));
        if (!retireResult)
            LOG_ERROR("[RenderRenderer] Renderer resource retirement failed: {}", retireResult.error().message);
    } else {
        releaseResources();
    }
    geometry_resources_ = nullptr;
    asset_gpu_registry_ = nullptr;
    material_cache_ = nullptr;
    device_resources_ = nullptr;
    light_environment_ = nullptr;
    initialized_ = false;
}

void RenderRenderer::enableIBL(RHIDevice& device, const std::string& hdrPath) {
    if (ibl_)
        return;

    FILE* test = nullptr;
#ifdef _WIN32
    fopen_s(&test, hdrPath.c_str(), "rb");
#else
    test = fopen(hdrPath.c_str(), "rb");
#endif
    if (!test) {
        LOG_WARN("[RenderRenderer] IBL requested but HDR file was not found: {}", hdrPath);
        return;
    }
    fclose(test);

    ibl_ = std::make_unique<IBLPipeline>();
    if (ibl_->bake(device, hdrPath)) {
        if (face_stage_) {
            face_stage_->setIBLTextures(ibl_->irradiance(), ibl_->prefilter(), ibl_->brdfLUT());
        }
        LOG_INFO("[RenderRenderer] IBL enabled: {}", hdrPath);
    } else {
        LOG_ERROR("[RenderRenderer] IBL bake failed: {}", hdrPath);
        ibl_.reset();
    }
}

ResultVoid RenderRenderer::preparePersistentResources(DeviceResourceClientId client,
                                                      const RenderResourcePrepareList& prepare) {
    if (!initialized_ || !device_resources_) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render renderer is not initialized."));
    }
    return device_resources_->preparePersistentResources(client, prepare);
}

ResultVoid RenderRenderer::render(RHIDevice& device, const RenderSurfaceBinding& surface,
                                  const RenderRequest& request) {
    if (!initialized_) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render renderer is not initialized."));
    }
    if (!surface.isValid() || !validateOutput(surface, request)) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render output is invalid."));
    }

    auto compiled = compile(request);
    if (!compiled) {
        clearCompiledCommands();
        LOG_ERROR("[RenderRenderer] Frame compilation failed: {}", compiled.error().message);
        return std::unexpected(compiled.error());
    }

    auto commandList = beginFrame(device, surface, request);
    if (!commandList) {
        return std::unexpected(commandList.error());
    }
    auto* cmd = *commandList;

    RenderView renderView;
    renderView.viewMatrix = request.view.viewMatrix;
    renderView.projectionMatrix = request.view.projectionMatrix;
    renderView.cameraPosition = request.view.cameraPosition;
    renderView.width = request.output.width ? request.output.width : request.view.width;
    renderView.height = request.output.height ? request.output.height : request.view.height;
    renderView.showFaces = renderSurfacesEnabled(request.options);
    renderView.showEdges = renderEdgesEnabled(request.options);
    renderView.showOverlay = request.options.showOverlays;
    renderView.showViewCube = request.options.showViewCube;
    renderView.viewCubeLayout = request.options.viewCubeLayout;
    renderView.viewCubeInteraction = request.options.viewCubeInteraction;

    RenderTargetInfo frameTargetInfo;
    frameTargetInfo.width = renderView.width;
    frameTargetInfo.height = renderView.height;
    frameTargetInfo.hasDepth = true;
    frameTargetInfo.presentable = request.output.mode == RenderTargetMode::Present;

    RenderFrame frame{ *cmd, renderView, frameTargetInfo };
    if (geometry_resources_ && light_environment_) {
        geometry_resources_->uploadFrameData(buildDrawContext(*cmd, frame), *light_environment_);
    }
    executeStages(frame, request.textDraws);

    cmd->endRenderPass();

    return endFrame(device, surface, request);
}

bool RenderRenderer::validateOutput(const RenderSurfaceBinding& surface, const RenderRequest& request) const {
    switch (request.output.mode) {
    case RenderTargetMode::Present:
        if (!surface.swapChain) {
            LOG_ERROR("[RenderRenderer] Present request rejected: SwapChain is required");
            return false;
        }
        return true;
    case RenderTargetMode::Offscreen:
        if (!surface.renderTarget) {
            LOG_ERROR("[RenderRenderer] Offscreen request rejected: RenderTarget is required");
            return false;
        }
        return true;
    case RenderTargetMode::Capture:
        if (!surface.renderTarget) {
            LOG_ERROR("[RenderRenderer] Capture request rejected: RenderTarget is required");
            return false;
        }
        if (!request.output.readback) {
            LOG_WARN("[RenderRenderer] Capture request has readback disabled");
        }
        return true;
    }
    return false;
}

void RenderRenderer::clearCompiledCommands() {
    const std::span<const MeshDrawCommand> emptyCommands;
    if (face_stage_) {
        face_stage_->setDrawCommands(emptyCommands);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands(emptyCommands);
    }
    if (highlight_stage_) {
        highlight_stage_->setSurfaceDrawCommands(emptyCommands);
        highlight_stage_->setEdgeDrawCommands(emptyCommands);
    }
    scene_compiler_.clear();
    overlay_compiler_.clear();
    surface_commands_.clear();
    edge_commands_.clear();
    highlight_surface_commands_.clear();
    highlight_edge_commands_.clear();
    merged_scene_command_revision_ = 0;
    merged_overlay_command_revision_ = 0;
    merged_commands_valid_ = false;
}

ResultVoid RenderRenderer::compile(const RenderRequest& request) {
    if (!request.sceneWorld && !request.overlayWorld) {
        clearCompiledCommands();
        return {};
    }

    if (face_stage_) {
        face_stage_->setSurfaceTechnique(request.options.surfaceTechnique);
    }

    RenderCompileContext compileContext{
        .assets = *asset_gpu_registry_,
        .materials = *material_cache_,
        .surfacePipeline = face_stage_ ? face_stage_->pipelineState() : nullptr,
        .surfaceTangentPipeline = face_stage_ && request.options.surfaceTechnique == SurfaceTechnique::SurfacePBR
                                          ? face_stage_->tangentPipelineState()
                                          : nullptr,
        .edgePipeline = edge_stage_ ? edge_stage_->pipelineState() : nullptr,
        .highlightSurfacePipeline = highlight_stage_ ? highlight_stage_->surfacePipeline() : nullptr,
        .highlightSurfaceTangentPipeline = highlight_stage_ ? highlight_stage_->surfaceTangentPipeline() : nullptr,
        .highlightEdgePipeline = highlight_stage_ ? highlight_stage_->edgePipeline() : nullptr,
    };
    if (request.sceneWorld) {
        auto compiled =
                scene_compiler_.compile(*request.sceneWorld, request.options, compileContext, &request.view, true);
        if (!compiled)
            return compiled;
    } else {
        scene_compiler_.clear();
    }
    if (request.overlayWorld) {
        auto compiled =
                overlay_compiler_.compile(*request.overlayWorld, request.options, compileContext, nullptr, false);
        if (!compiled)
            return compiled;
    } else {
        overlay_compiler_.clear();
    }

    const uint64_t sceneCommandRevision = scene_compiler_.commandRevision();
    const uint64_t overlayCommandRevision = overlay_compiler_.commandRevision();
    if (merged_commands_valid_ && merged_scene_command_revision_ == sceneCommandRevision &&
        merged_overlay_command_revision_ == overlayCommandRevision) {
        return {};
    }

    const auto mergeCommands = [](std::vector<MeshDrawCommand>& destination,
                                  std::span<const MeshDrawCommand> sceneCommands,
                                  std::span<const MeshDrawCommand> overlayCommands) {
        destination.clear();
        destination.reserve(sceneCommands.size() + overlayCommands.size());
        destination.insert(destination.end(), sceneCommands.begin(), sceneCommands.end());
        destination.insert(destination.end(), overlayCommands.begin(), overlayCommands.end());
    };
    mergeCommands(surface_commands_, scene_compiler_.surfaceCommands(), overlay_compiler_.surfaceCommands());
    mergeCommands(edge_commands_, scene_compiler_.edgeCommands(), overlay_compiler_.edgeCommands());
    mergeCommands(highlight_surface_commands_, scene_compiler_.highlightSurfaceCommands(),
                  overlay_compiler_.highlightSurfaceCommands());
    mergeCommands(highlight_edge_commands_, scene_compiler_.highlightEdgeCommands(),
                  overlay_compiler_.highlightEdgeCommands());

    const std::span<const MeshDrawCommand> emptyCommands;
    if (face_stage_) {
        face_stage_->setDrawCommands(!surface_commands_.empty() ? std::span<const MeshDrawCommand>(surface_commands_)
                                                                : emptyCommands);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands(!edge_commands_.empty() ? std::span<const MeshDrawCommand>(edge_commands_)
                                                             : emptyCommands);
    }
    if (highlight_stage_) {
        highlight_stage_->setSurfaceDrawCommands(!highlight_surface_commands_.empty()
                                                         ? std::span<const MeshDrawCommand>(highlight_surface_commands_)
                                                         : emptyCommands);
        highlight_stage_->setEdgeDrawCommands(!highlight_edge_commands_.empty()
                                                      ? std::span<const MeshDrawCommand>(highlight_edge_commands_)
                                                      : emptyCommands);
    }
    merged_scene_command_revision_ = sceneCommandRevision;
    merged_overlay_command_revision_ = overlayCommandRevision;
    merged_commands_valid_ = true;
    return {};
}

Result<CommandList*> RenderRenderer::beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                                const RenderRequest& request) {
    auto commandListResult = device.beginFrame(surface.swapChain ? surface.swapChain : nullptr);
    if (!commandListResult) {
        LOG_ERROR("[RenderRenderer] Frame begin failed: {}", commandListResult.error().message);
        return std::unexpected(commandListResult.error());
    }
    auto* cmd = *commandListResult;

    if (request.output.mode == RenderTargetMode::Present) {
        cmd->beginRenderPass(surface.swapChain->renderPassBeginInfo());
    } else {
        cmd->beginRenderPass(surface.renderTarget->renderPassBeginInfo());
    }

    Viewport vp;
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(request.view.width);
    vp.height = static_cast<float>(request.view.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->setViewport(vp);

    ScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<int32_t>(request.view.width);
    scissor.height = static_cast<int32_t>(request.view.height);
    cmd->setScissorRect(scissor);
    return cmd;
}

void RenderRenderer::executeStages(RenderFrame& frame, const TextDrawList& requestTextDraws) {
    if (text_stage_)
        text_stage_->beginFrame(frame.view.width, frame.view.height);
    TextDrawList textDraws;
    textDraws.append(requestTextDraws);
    if (face_stage_)
        face_stage_->execute(frame);
    if (edge_stage_)
        edge_stage_->execute(frame);
    if (highlight_stage_)
        highlight_stage_->execute(frame);
    if (view_cube_stage_ && frame.view.showOverlay && frame.view.showViewCube) {
        view_cube_stage_->setPipelines(face_stage_ ? face_stage_->viewCubePipelineState() : nullptr,
                                       edge_stage_ ? edge_stage_->viewCubePipelineState() : nullptr);
        view_cube_stage_->setFallbackResources(face_stage_ ? face_stage_->defaultWhiteTexture() : nullptr,
                                               face_stage_ ? face_stage_->defaultSampler() : nullptr);
        view_cube_stage_->setLayout(frame.view.viewCubeLayout);
        view_cube_stage_->setInteraction(frame.view.viewCubeInteraction);
        view_cube_stage_->execute(frame);
        view_cube_stage_->collectLabels(textDraws, frame.view.viewMatrix, frame.view.width, frame.view.height);
    }
    if (text_stage_) {
        text_stage_->addTextList(textDraws);
        text_stage_->execute(frame);
    }
}

DrawExecutionContext RenderRenderer::buildDrawContext(CommandList& cmd, const RenderFrame& frame) const {
    DrawExecutionContext ctx;
    ctx.cmd = &cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    return ctx;
}

ResultVoid RenderRenderer::endFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                    const RenderRequest& request) {
    SwapChain* swapchain = request.output.mode == RenderTargetMode::Present ? surface.swapChain : nullptr;
    auto result = device.endFrame(swapchain);
    if (!result) {
        LOG_ERROR("[RenderRenderer] Frame submission failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    return {};
}

}  // namespace mulan::engine
