#include "render_renderer.h"

#include "../frame/render_frame.h"
#include "../overlay/view_cube_stage.h"
#include "../text/text_stage.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../rhi/render_target.h"
#include "../../rhi/render_types.h"
#include "../../rhi/swap_chain.h"

#include <cstdio>
#include <span>

namespace mulan::engine {
namespace {

void prepareTexture(AssetGpuRegistry& assets, const RenderTextureDesc& desc) {
    if (!desc.resourceKey || !desc.image || !desc.image->valid())
        return;

    TextureLoadOptions options;
    options.sRGB = desc.srgb;
    assets.acquireTexture(desc.resourceKey, *desc.image, options);
}

void prepareMaterialTextures(AssetGpuRegistry& assets, const RenderMaterialDesc& material) {
    prepareTexture(assets, material.baseColorTexture);
    prepareTexture(assets, material.normalTexture);
    prepareTexture(assets, material.metallicRoughnessTexture);
    prepareTexture(assets, material.emissiveTexture);
    prepareTexture(assets, material.ambientOcclusionTexture);
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

    view_cube_stage_ = std::make_unique<ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, targetInfo)) {
        std::fprintf(stderr, "[RenderRenderer] ViewCube init failed (non-fatal)\n");
        view_cube_stage_.reset();
    }

    text_stage_ = std::make_unique<TextStage>(device);
    if (!text_stage_->init(device, targetInfo)) {
        std::fprintf(stderr, "[RenderRenderer] TextStage init failed (non-fatal)\n");
        text_stage_.reset();
    }

    initialized_ = true;
    return true;
}

void RenderRenderer::shutdown(RHIDevice& device) {
    if (!initialized_)
        return;
    device.waitIdle();
    text_stage_.reset();
    view_cube_stage_.reset();
    edge_stage_.reset();
    face_stage_.reset();
    geometry_resources_.reset();
    asset_gpu_registry_.reset();
    material_cache_.reset();
    ibl_.reset();
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
        std::fprintf(stderr,
                     "[RenderRenderer] IBL requested but HDR not found: %s "
                     "(place a .hdr file there)\n",
                     hdrPath.c_str());
        return;
    }
    fclose(test);

    ibl_ = std::make_unique<IBLPipeline>();
    if (ibl_->bake(device, hdrPath)) {
        if (face_stage_) {
            face_stage_->setIBLTextures(ibl_->irradiance(), ibl_->prefilter(), ibl_->brdfLUT());
        }
    } else {
        ibl_.reset();
    }
}

void RenderRenderer::render(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request) {
    if (!initialized_ || !surface.isValid())
        return;
    if (!validateOutput(surface, request))
        return;

    device.beginUploadBatch();
    compile(request);
    device.flushUploadBatch();

    auto* cmd = beginFrame(device, surface, request);
    if (!cmd)
        return;

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
    executeStages(frame);

    cmd->endRenderPass();
    cmd->end();

    endFrame(device, surface, request);
}

void RenderRenderer::clearAssetResources(RHIDevice& device) {
    device.waitIdle();
    clearCompiledCommands();
    if (asset_gpu_registry_) {
        asset_gpu_registry_->clear();
    }
}

bool RenderRenderer::validateOutput(const RenderSurfaceBinding& surface, const RenderRequest& request) const {
    switch (request.output.mode) {
    case RenderTargetMode::Present:
        if (!surface.swapChain) {
            std::fprintf(stderr, "[RenderRenderer] Present request requires a SwapChain\n");
            return false;
        }
        return true;
    case RenderTargetMode::Offscreen:
        if (!surface.renderTarget) {
            std::fprintf(stderr, "[RenderRenderer] Offscreen request requires a RenderTarget\n");
            return false;
        }
        return true;
    case RenderTargetMode::Capture:
        if (!surface.renderTarget) {
            std::fprintf(stderr, "[RenderRenderer] Capture request requires a RenderTarget\n");
            return false;
        }
        if (!request.output.readback) {
            std::fprintf(stderr, "[RenderRenderer] Capture request should enable readback\n");
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
    compiler_.clear();
}

void RenderRenderer::compile(const RenderRequest& request) {
    if (!request.world) {
        clearCompiledCommands();
        return;
    }

    workload_.build(*request.world, request.options);
    prepareResources(request);

    if (face_stage_) {
        face_stage_->setSurfaceTechnique(request.options.surfaceTechnique);
    }

    RenderCompileContext compileContext{
        .assets = *asset_gpu_registry_,
        .materials = *material_cache_,
        .surfacePipeline = face_stage_ ? face_stage_->pipelineState() : nullptr,
        .edgePipeline = edge_stage_ ? edge_stage_->pipelineState() : nullptr,
    };
    compiler_.compile(*request.world, workload_, compileContext);

    const std::span<const MeshDrawCommand> emptyCommands;
    if (face_stage_) {
        face_stage_->setDrawCommands(renderSurfacesEnabled(request.options) ? compiler_.surfaceCommands()
                                                                            : emptyCommands);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands((renderEdgesEnabled(request.options) || request.options.hasHoveredPickId)
                                             ? compiler_.edgeCommands()
                                             : emptyCommands);
    }
}

void RenderRenderer::prepareResources(const RenderRequest& request) {
    if (!request.world || !asset_gpu_registry_)
        return;

    if (request.prepare) {
        for (const auto& geometry : request.prepare->geometries()) {
            if (geometry.mesh && !geometry.mesh->empty()) {
                asset_gpu_registry_->acquireGeometry(geometry.resourceKey, *geometry.mesh);
            }
        }
    }

    if (request.options.surfaceTechnique == SurfaceTechnique::SurfacePBR) {
        for (const auto& item : workload_.surfaces()) {
            const auto* material = request.world->material(item.material);
            if (material) {
                prepareMaterialTextures(*asset_gpu_registry_, material->desc);
            }
        }
    }
}

CommandList* RenderRenderer::beginFrame(RHIDevice& device, const RenderSurfaceBinding& surface,
                                        const RenderRequest& request) {
    device.beginFrame(surface.swapChain ? surface.swapChain : nullptr);
    auto* cmd = device.frameCommandList();
    if (!cmd)
        return nullptr;
    cmd->begin();

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

void RenderRenderer::executeStages(RenderFrame& frame) {
    if (text_stage_)
        text_stage_->beginFrame(frame.view.width, frame.view.height);
    TextDrawList textDraws;
    if (face_stage_)
        face_stage_->execute(frame);
    if (edge_stage_)
        edge_stage_->execute(frame);
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

void RenderRenderer::endFrame(RHIDevice& device, const RenderSurfaceBinding& surface, const RenderRequest& request) {
    if (request.output.mode == RenderTargetMode::Present) {
        device.submitAndPresent(surface.swapChain);
    } else {
        device.submitOffscreen();
    }
}

}  // namespace mulan::engine
