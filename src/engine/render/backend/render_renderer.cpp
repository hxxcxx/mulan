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
namespace {

core::Result<void> prepareTexture(AssetGpuRegistry& assets, const RenderTextureDesc& desc) {
    if (!desc.resourceKey || !desc.image || !desc.image->valid())
        return {};

    TextureLoadOptions options;
    options.sRGB = desc.srgb;
    auto texture = assets.acquireTexture(desc.resourceKey, *desc.image, options);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    return {};
}

core::Result<void> prepareMaterialTextures(AssetGpuRegistry& assets, const RenderMaterialDesc& material) {
    if (auto result = prepareTexture(assets, material.baseColorTexture); !result)
        return std::unexpected(result.error());
    if (auto result = prepareTexture(assets, material.normalTexture); !result)
        return std::unexpected(result.error());
    if (auto result = prepareTexture(assets, material.metallicRoughnessTexture); !result)
        return std::unexpected(result.error());
    if (auto result = prepareTexture(assets, material.emissiveTexture); !result)
        return std::unexpected(result.error());
    if (auto result = prepareTexture(assets, material.ambientOcclusionTexture); !result)
        return std::unexpected(result.error());
    return {};
}

}  // namespace

RenderRenderer::RenderRenderer() = default;
RenderRenderer::~RenderRenderer() = default;

bool RenderRenderer::init(RHIDevice& device, LightEnvironment& lightEnv, TextureFormat colorFmt, TextureFormat depthFmt,
                          uint32_t sampleCount) {
    if (initialized_)
        return true;

    material_cache_ = std::make_unique<MaterialCache>();
    asset_gpu_registry_ = std::make_unique<AssetGpuRegistry>(device);
    geometry_resources_ = std::make_unique<GeometryDrawSharedResources>(device, *material_cache_, lightEnv);
    if (!geometry_resources_->init()) {
        return false;
    }

    RenderTargetInfo targetInfo;
    targetInfo.colorFormat = colorFmt;
    targetInfo.depthFormat = depthFmt;
    targetInfo.hasDepth = true;
    targetInfo.sampleCount = sampleCount;

    face_stage_ = std::make_unique<FaceStage>(device, *geometry_resources_);
    if (!face_stage_->init(device, targetInfo)) {
        return false;
    }

    edge_stage_ = std::make_unique<EdgeStage>(device, *geometry_resources_);
    if (!edge_stage_->init(device, targetInfo)) {
        return false;
    }

    highlight_stage_ = std::make_unique<HighlightStage>(device, *geometry_resources_);
    if (!highlight_stage_->init(device, targetInfo)) {
        return false;
    }

    view_cube_stage_ = std::make_unique<ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, targetInfo)) {
        LOG_WARN("[RenderRenderer] ViewCube stage initialization failed; continuing without it");
        view_cube_stage_.reset();
    }

    text_stage_ = std::make_unique<TextStage>(device);
    if (!text_stage_->init(device, targetInfo)) {
        LOG_WARN("[RenderRenderer] Text stage initialization failed; continuing without it");
        text_stage_.reset();
    }

    initialized_ = true;
    LOG_INFO("[RenderRenderer] Initialized: colorFormat={}, depthFormat={}, sampleCount={}, viewCube={}, text={}",
             static_cast<int>(colorFmt), static_cast<int>(depthFmt), sampleCount, view_cube_stage_ != nullptr,
             text_stage_ != nullptr);
    return true;
}

void RenderRenderer::shutdown(RHIDevice& device) {
    // 即使 init() 未能完成（initialized_ 仍为 false），也可能已分配部分 RHI 资源
    //（如 GeometryDrawSharedResources 的 UBO）。因此 shutdown 必须无条件执行清理，
    // 否则这些资源会在 device 析构时触发 assertNoLiveResources 断言。
    clearCompiledCommands();

    auto releaseResources = [textStage = std::move(text_stage_), viewCubeStage = std::move(view_cube_stage_),
                             highlightStage = std::move(highlight_stage_), edgeStage = std::move(edge_stage_),
                             faceStage = std::move(face_stage_), ibl = std::move(ibl_),
                             geometryResources = std::move(geometry_resources_),
                             assetRegistry = std::move(asset_gpu_registry_),
                             materialCache = std::move(material_cache_)]() mutable {
        textStage.reset();
        viewCubeStage.reset();
        highlightStage.reset();
        edgeStage.reset();
        faceStage.reset();
        ibl.reset();
        geometryResources.reset();
        assetRegistry.reset();
        materialCache.reset();
    };

    const SubmissionToken token = device.lastSubmissionToken();
    if (token) {
        auto retireResult = device.retire(token, std::move(releaseResources));
        if (!retireResult)
            LOG_ERROR("[RenderRenderer] Renderer resource retirement failed: {}", retireResult.error().message);
    } else {
        releaseResources();
    }
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

core::Result<void> RenderRenderer::preparePersistentResources(RHIDevice& device,
                                                              const RenderResourcePrepareList& prepare) {
    if (!initialized_ || !asset_gpu_registry_) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Render renderer is not initialized."));
    }
    if (prepare.empty()) {
        return {};
    }

    if (auto result = device.beginUploadBatch(); !result) {
        LOG_ERROR("[RenderRenderer] Persistent upload batch begin failed: {}", result.error().message);
        return std::unexpected(result.error());
    }

    std::optional<core::Error> prepareFailure;
    for (const auto& geometry : prepare.geometries()) {
        if (!geometry.mesh || geometry.mesh->empty()) {
            continue;
        }
        auto acquired =
                asset_gpu_registry_->acquireGeometry(geometry.resourceKey, *geometry.mesh, geometry.forceUpdate);
        if (!acquired) {
            prepareFailure = acquired.error();
            break;
        }
    }

    // 即使单个资源创建失败，也要结束已开启的批次，避免后端停留在半开启状态。
    if (auto result = device.flushUploadBatch(); !result) {
        LOG_ERROR("[RenderRenderer] Persistent upload batch flush failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    asset_gpu_registry_->releaseUploadFailureKeepalives();
    if (prepareFailure) {
        LOG_ERROR("[RenderRenderer] Persistent resource preparation failed: {}", prepareFailure->message);
        return std::unexpected(std::move(*prepareFailure));
    }
    return {};
}

core::Result<void> RenderRenderer::render(RHIDevice& device, const RenderSurfaceBinding& surface,
                                          const RenderRequest& request) {
    if (!initialized_) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Render renderer is not initialized."));
    }
    if (!surface.isValid() || !validateOutput(surface, request)) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Render output is invalid."));
    }

    if (auto result = device.beginUploadBatch(); !result) {
        LOG_ERROR("[RenderRenderer] Upload batch begin failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    auto compiled = compile(request);
    if (auto result = device.flushUploadBatch(); !result) {
        LOG_ERROR("[RenderRenderer] Upload batch flush failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    asset_gpu_registry_->releaseUploadFailureKeepalives();
    if (!compiled) {
        clearCompiledCommands();
        LOG_ERROR("[RenderRenderer] Frame resource preparation failed: {}", compiled.error().message);
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
    if (geometry_resources_) {
        geometry_resources_->uploadFrameData(buildDrawContext(*cmd, frame));
    }
    executeStages(frame, request.textDraws);

    cmd->endRenderPass();

    return endFrame(device, surface, request);
}

void RenderRenderer::clearAssetResources(RHIDevice& device) {
    clearCompiledCommands();
    if (!asset_gpu_registry_)
        return;

    auto retiredRegistry = std::move(asset_gpu_registry_);
    asset_gpu_registry_ = std::make_unique<AssetGpuRegistry>(device);

    const SubmissionToken token = device.lastSubmissionToken();
    if (!token)
        return;
    auto retireResult = device.retire(token, [registry = std::move(retiredRegistry)]() mutable { registry.reset(); });
    if (!retireResult)
        LOG_ERROR("[RenderRenderer] Asset resource retirement failed: {}", retireResult.error().message);
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
    workload_.clear();
    compiler_.clear();
}

core::Result<void> RenderRenderer::compile(const RenderRequest& request) {
    if (!request.world) {
        clearCompiledCommands();
        return {};
    }

    workload_.build(*request.world, request.options);
    if (auto result = prepareFrameResources(request); !result) {
        return std::unexpected(result.error());
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
    compiler_.compile(*request.world, workload_, compileContext);

    const std::span<const MeshDrawCommand> emptyCommands;
    if (face_stage_) {
        face_stage_->setDrawCommands(!compiler_.surfaceCommands().empty() ? compiler_.surfaceCommands()
                                                                          : emptyCommands);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands(!compiler_.edgeCommands().empty() ? compiler_.edgeCommands() : emptyCommands);
    }
    if (highlight_stage_) {
        highlight_stage_->setSurfaceDrawCommands(
                !compiler_.highlightSurfaceCommands().empty() ? compiler_.highlightSurfaceCommands() : emptyCommands);
        highlight_stage_->setEdgeDrawCommands(
                !compiler_.highlightEdgeCommands().empty() ? compiler_.highlightEdgeCommands() : emptyCommands);
    }
    return {};
}

core::Result<void> RenderRenderer::prepareFrameResources(const RenderRequest& request) {
    if (!request.world || !asset_gpu_registry_)
        return {};

    if (request.options.surfaceTechnique == SurfaceTechnique::SurfacePBR) {
        for (const auto& item : workload_.surfaces()) {
            const auto* material = request.world->material(item.material);
            if (material) {
                if (auto result = prepareMaterialTextures(*asset_gpu_registry_, material->desc); !result) {
                    return std::unexpected(result.error());
                }
            }
        }
    }
    return {};
}

core::Result<CommandList*> RenderRenderer::beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
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

core::Result<void> RenderRenderer::endFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
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
