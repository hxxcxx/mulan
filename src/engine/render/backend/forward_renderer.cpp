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

ResultVoid ForwardRenderer::render(RHIDevice& device, const RenderOutput& output, const RenderRequest& request,
                                   const LightEnvironment& lightEnvironment) {
    MULAN_PROFILE_ZONE();

    if (!initialized_) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Forward renderer is not initialized."));
    }
    auto compiled = compile(request);
    if (!compiled) {
        clearCompiledCommands();
        LOG_ERROR("[ForwardRenderer] Frame compilation failed: {}", compiled.error().message);
        return std::unexpected(compiled.error());
    }

    auto commandList = beginFrame(device, output, request.view);
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

    return endFrame(device, output);
}

void ForwardRenderer::clearCompiledCommands() {
    scene_compiler_.clear();
    overlay_compiler_.clear();
    publishCompiledCommands();
}

void ForwardRenderer::publishCompiledCommands() {
    // Compiler 拥有命令内存；Stage 只借用无需分组的 span，并复制自身确实需要
    // 按 pipeline family 拆分的表面命令。两个来源的 revision 独立判定更新。
    const CompiledDrawCommandSet scene = scene_compiler_.drawCommands();
    const CompiledDrawCommandSet overlay = overlay_compiler_.drawCommands();

    if (face_stage_) {
        face_stage_->setDrawCommands(CommandSource::Scene, scene.revision, scene.surfaces);
        face_stage_->setDrawCommands(CommandSource::Overlay, overlay.revision, overlay.surfaces);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands(CommandSource::Scene, scene.revision, scene.edges);
        edge_stage_->setDrawCommands(CommandSource::Overlay, overlay.revision, overlay.edges);
    }
    if (highlight_stage_) {
        highlight_stage_->setDrawCommands(CommandSource::Scene, scene.revision, scene.highlightSurfaces,
                                          scene.highlightEdges);
        highlight_stage_->setDrawCommands(CommandSource::Overlay, overlay.revision, overlay.highlightSurfaces,
                                          overlay.highlightEdges);
    }
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

    publishCompiledCommands();
    return {};
}

Result<CommandList*> ForwardRenderer::beginFrame(RHIDevice& device, const RenderOutput& output,
                                                 const RenderViewDesc& view) {
    MULAN_PROFILE_ZONE();

    auto commandListResult = device.beginFrame(output.swapChain());
    if (!commandListResult) {
        LOG_ERROR("[ForwardRenderer] Frame begin failed: {}", commandListResult.error().message);
        return std::unexpected(commandListResult.error());
    }
    auto* cmd = *commandListResult;

    const RenderPassBeginInfo renderPass = output.isPresentable() ? output.swapChain()->renderPassBeginInfo()
                                                                  : output.renderTarget()->renderPassBeginInfo();
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

ResultVoid ForwardRenderer::endFrame(RHIDevice& device, const RenderOutput& output) {
    MULAN_PROFILE_ZONE();

    auto result = device.endFrame(output.swapChain());
    if (!result) {
        LOG_ERROR("[ForwardRenderer] Frame submission failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    return {};
}

}  // namespace mulan::engine
