#include "forward_renderer.h"

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
#include <mulan/core/profiling/profile.h>

#include <cstdio>
#include <span>

namespace mulan::engine {
ForwardRenderer::ForwardRenderer() = default;
ForwardRenderer::~ForwardRenderer() = default;

ResultVoid ForwardRenderer::init(RHIDevice& device, DeviceResourceService& resources, const RenderTargetInfo& target) {
    MULAN_PROFILE_ZONE();

    if (initialized_)
        return {};

    material_cache_ = &resources.materials();
    asset_gpu_registry_ = &resources.assets();
    geometry_resources_ = &resources.geometryDrawResources();
    fallback_resources_ = &resources.drawFallbackResources();

    face_stage_ =
            std::make_unique<FaceStage>(device, *geometry_resources_, *fallback_resources_, resources.pipelines());
    if (auto initialized = face_stage_->init(device, target); !initialized)
        return std::unexpected(initialized.error());

    edge_stage_ =
            std::make_unique<EdgeStage>(device, *geometry_resources_, *fallback_resources_, resources.pipelines());
    if (auto initialized = edge_stage_->init(device, target); !initialized)
        return std::unexpected(initialized.error());

    highlight_stage_ =
            std::make_unique<HighlightStage>(device, *geometry_resources_, *fallback_resources_, resources.pipelines());
    if (auto initialized = highlight_stage_->init(device, target); !initialized)
        return std::unexpected(initialized.error());

    view_cube_stage_ = std::make_unique<ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, target)) {
        LOG_WARN("[ForwardRenderer] ViewCube stage initialization failed; continuing without it");
        view_cube_stage_.reset();
    }

    text_stage_ = resources.acquireTextStage(target);
    if (!text_stage_) {
        LOG_WARN("[ForwardRenderer] Text stage initialization failed; continuing without it");
    }

    initialized_ = true;
    LOG_INFO("[ForwardRenderer] Initialized: colorFormat={}, depthFormat={}, sampleCount={}, viewCube={}, text={}",
             static_cast<int>(target.colorFormat), static_cast<int>(target.depthFormat), target.sampleCount,
             view_cube_stage_ != nullptr, text_stage_ != nullptr);
    return {};
}

void ForwardRenderer::shutdown(RHIDevice& device) {
    // 即使 init() 未能完成（initialized_ 仍为 false），Stage 也可能已分配部分管线或绑定资源。
    // 因此 shutdown 必须无条件执行清理，否则会在 Device 析构时触发存活资源断言。
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
            LOG_ERROR("[ForwardRenderer] Renderer resource retirement failed: {}", retireResult.error().message);
    } else {
        releaseResources();
    }
    geometry_resources_ = nullptr;
    fallback_resources_ = nullptr;
    asset_gpu_registry_ = nullptr;
    material_cache_ = nullptr;
    initialized_ = false;
}

void ForwardRenderer::enableIBL(RHIDevice& device, const std::string& hdrPath) {
    if (ibl_)
        return;

    FILE* test = nullptr;
#ifdef _WIN32
    fopen_s(&test, hdrPath.c_str(), "rb");
#else
    test = fopen(hdrPath.c_str(), "rb");
#endif
    if (!test) {
        LOG_WARN("[ForwardRenderer] IBL requested but HDR file was not found: {}", hdrPath);
        return;
    }
    fclose(test);

    ibl_ = std::make_unique<IBLPipeline>();
    if (ibl_->bake(device, hdrPath)) {
        if (face_stage_) {
            face_stage_->setIBLTextures(ibl_->irradiance(), ibl_->prefilter(), ibl_->brdfLUT());
        }
        LOG_INFO("[ForwardRenderer] IBL enabled: {}", hdrPath);
    } else {
        LOG_ERROR("[ForwardRenderer] IBL bake failed: {}", hdrPath);
        ibl_.reset();
    }
}

ResultVoid ForwardRenderer::render(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request,
                                   const LightEnvironment& lightEnvironment) {
    MULAN_PROFILE_ZONE();

    if (!initialized_) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Forward renderer is not initialized."));
    }
    if (!validateSurface(surface)) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render output is invalid."));
    }

    auto compiled = compile(request);
    if (!compiled) {
        clearCompiledCommands();
        LOG_ERROR("[ForwardRenderer] Frame compilation failed: {}", compiled.error().message);
        return std::unexpected(compiled.error());
    }

    auto commandList = beginFrame(device, surface, request.view);
    if (!commandList) {
        return std::unexpected(commandList.error());
    }
    auto* cmd = *commandList;

    RenderView renderView;
    renderView.viewMatrix = request.view.viewMatrix;
    renderView.projectionMatrix = request.view.projectionMatrix;
    renderView.cameraPosition = request.view.cameraPosition;
    renderView.width = request.view.width;
    renderView.height = request.view.height;
    renderView.showOverlay = request.options.showOverlays;
    renderView.showViewCube = request.options.showViewCube;
    renderView.viewCubeLayout = request.options.viewCubeLayout;
    renderView.viewCubeInteraction = request.options.viewCubeInteraction;

    RenderFrame frame{ *cmd, renderView };
    if (geometry_resources_) {
        geometry_resources_->uploadFrameData(buildDrawContext(*cmd, frame), lightEnvironment);
    }
    executeStages(frame);

    cmd->endRenderPass();

    return endFrame(device, surface);
}

bool ForwardRenderer::validateSurface(const RenderSurfaceBinding& surface) const {
    if (!surface.isValid()) {
        LOG_ERROR("[ForwardRenderer] Exactly one SwapChain or RenderTarget must be bound");
        return false;
    }
    return true;
}

void ForwardRenderer::clearCompiledCommands() {
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

ResultVoid ForwardRenderer::compile(const RenderRequest& request) {
    MULAN_PROFILE_ZONE();

    if (!request.sceneWorld && !request.overlayWorld) {
        clearCompiledCommands();
        return {};
    }

    RenderCompileContext compileContext{
        .assets = *asset_gpu_registry_,
        .materials = *material_cache_,
        .surfacePipelines = face_stage_.get(),
        .edgePipeline = edge_stage_ ? edge_stage_->pipelineState() : nullptr,
        .highlightSurfacePipeline = highlight_stage_ ? highlight_stage_->surfacePipeline() : nullptr,
        .highlightSurfaceTangentPipeline = highlight_stage_ ? highlight_stage_->surfaceTangentPipeline() : nullptr,
        .highlightEdgePipeline = highlight_stage_ ? highlight_stage_->edgePipeline() : nullptr,
    };
    if (request.sceneWorld) {
        auto compiled = [&] {
            MULAN_PROFILE_ZONE_N("Render/CompileScene");
            return scene_compiler_.compile(*request.sceneWorld, request.options, compileContext, &request.view, true);
        }();
        if (!compiled)
            return compiled;
    } else {
        scene_compiler_.clear();
    }
    if (request.overlayWorld) {
        auto compiled = [&] {
            MULAN_PROFILE_ZONE_N("Render/CompileOverlay");
            return overlay_compiler_.compile(*request.overlayWorld, request.options, compileContext, nullptr, false);
        }();
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

Result<CommandList*> ForwardRenderer::beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                                 const RenderViewDesc& view) {
    MULAN_PROFILE_ZONE();

    auto commandListResult = device.beginFrame(surface.swapChain ? surface.swapChain : nullptr);
    if (!commandListResult) {
        LOG_ERROR("[ForwardRenderer] Frame begin failed: {}", commandListResult.error().message);
        return std::unexpected(commandListResult.error());
    }
    auto* cmd = *commandListResult;

    const RenderPassBeginInfo renderPass = surface.isPresentable() ? surface.swapChain->renderPassBeginInfo()
                                                                   : surface.renderTarget->renderPassBeginInfo();
    cmd->beginRenderPass(renderPass);

    Viewport vp;
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(view.width);
    vp.height = static_cast<float>(view.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->setViewport(vp);

    ScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<int32_t>(view.width);
    scissor.height = static_cast<int32_t>(view.height);
    cmd->setScissorRect(scissor);
    return cmd;
}

void ForwardRenderer::executeStages(RenderFrame& frame) {
    MULAN_PROFILE_ZONE();

    if (text_stage_)
        text_stage_->beginFrame(frame.view.width, frame.view.height);
    TextDrawList textDraws;
    if (face_stage_) {
        MULAN_PROFILE_ZONE_N("RenderStage/Faces");
        face_stage_->execute(frame);
    }
    if (edge_stage_) {
        MULAN_PROFILE_ZONE_N("RenderStage/Edges");
        edge_stage_->execute(frame);
    }
    if (highlight_stage_) {
        MULAN_PROFILE_ZONE_N("RenderStage/Highlight");
        highlight_stage_->execute(frame);
    }
    if (view_cube_stage_ && frame.view.showOverlay && frame.view.showViewCube) {
        MULAN_PROFILE_ZONE_N("RenderStage/ViewCube");
        view_cube_stage_->setPipelines(face_stage_ ? face_stage_->viewCubePipelineState() : nullptr,
                                       edge_stage_ ? edge_stage_->viewCubePipelineState() : nullptr);
        view_cube_stage_->setFallbackResources(fallback_resources_ ? fallback_resources_->whiteTexture() : nullptr,
                                               fallback_resources_ ? fallback_resources_->sampler() : nullptr);
        view_cube_stage_->setLayout(frame.view.viewCubeLayout);
        view_cube_stage_->setInteraction(frame.view.viewCubeInteraction);
        view_cube_stage_->execute(frame);
        view_cube_stage_->collectLabels(textDraws, frame.view.viewMatrix, frame.view.width, frame.view.height);
    }
    if (text_stage_) {
        MULAN_PROFILE_ZONE_N("RenderStage/Text");
        text_stage_->addTextList(textDraws);
        text_stage_->execute(frame);
    }
}

DrawExecutionContext ForwardRenderer::buildDrawContext(CommandList& cmd, const RenderFrame& frame) const {
    DrawExecutionContext ctx;
    ctx.cmd = &cmd;
    ctx.width = static_cast<int>(frame.view.width);
    ctx.height = static_cast<int>(frame.view.height);
    ctx.camera.viewMatrix = frame.view.viewMatrix;
    ctx.camera.projectionMatrix = frame.view.projectionMatrix;
    ctx.camera.eyePosition = frame.view.cameraPosition;
    return ctx;
}

ResultVoid ForwardRenderer::endFrame(RHIDevice& device, const RenderSurfaceBinding& surface) {
    MULAN_PROFILE_ZONE();

    auto result = device.endFrame(surface.swapChain);
    if (!result) {
        LOG_ERROR("[ForwardRenderer] Frame submission failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    return {};
}

}  // namespace mulan::engine
