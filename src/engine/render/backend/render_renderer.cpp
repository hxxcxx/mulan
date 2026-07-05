#include "render_renderer.h"

#include "../frame/render_frame.h"
#include "../overlay/view_cube_stage.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../rhi/render_target.h"
#include "../../rhi/render_types.h"
#include "../../rhi/swap_chain.h"

#include <cstdio>
#include <span>

namespace mulan::engine {

RenderRenderer::RenderRenderer() = default;
RenderRenderer::~RenderRenderer() = default;

bool RenderRenderer::init(RHIDevice& device,
                          LightEnvironment& lightEnv,
                          TextureFormat colorFmt,
                          TextureFormat depthFmt) {
    if (initialized_) return true;

    texture_cache_ = std::make_unique<TextureCache>(&device);
    material_cache_ = std::make_unique<MaterialCache>();
    resources_ = std::make_unique<RenderResourceCache>(device);

    RenderTargetInfo targetInfo;
    targetInfo.colorFormat = colorFmt;
    targetInfo.depthFormat = depthFmt;
    targetInfo.hasDepth = true;

    face_stage_ = std::make_unique<FaceStage>(device, *resources_, *material_cache_, lightEnv);
    if (!face_stage_->init(device, targetInfo)) {
        return false;
    }

    edge_stage_ = std::make_unique<EdgeStage>(device, *resources_, *material_cache_, lightEnv);
    if (!edge_stage_->init(device, targetInfo)) {
        return false;
    }

    view_cube_stage_ = std::make_unique<ViewCubeStage>(device);
    if (!view_cube_stage_->init(device, targetInfo)) {
        std::fprintf(stderr, "[RenderRenderer] ViewCube init failed (non-fatal)\n");
        view_cube_stage_.reset();
    }

    initialized_ = true;
    return true;
}

void RenderRenderer::shutdown(RHIDevice& device) {
    if (!initialized_) return;
    device.waitIdle();
    view_cube_stage_.reset();
    edge_stage_.reset();
    face_stage_.reset();
    resources_.reset();
    material_cache_.reset();
    texture_cache_.reset();
    ibl_.reset();
    initialized_ = false;
}

void RenderRenderer::enableIBL(RHIDevice& device, const std::string& hdrPath) {
    if (ibl_) return;

    FILE* test = nullptr;
#ifdef _WIN32
    fopen_s(&test, hdrPath.c_str(), "rb");
#else
    test = fopen(hdrPath.c_str(), "rb");
#endif
    if (!test) {
        std::fprintf(stderr, "[RenderRenderer] IBL requested but HDR not found: %s "
                             "(place a .hdr file there)\n", hdrPath.c_str());
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

void RenderRenderer::render(RHIDevice& device, const RenderSurfaceBinding& surface,
                            const RenderRequest& request) {
    if (!initialized_ || !surface.isValid()) return;

    device.beginUploadBatch();
    compile(request);
    device.flushUploadBatch();

    auto* cmd = beginFrame(device, surface, request);
    if (!cmd) return;

    RenderView renderView;
    renderView.viewMatrix = request.view.viewMatrix;
    renderView.projectionMatrix = request.view.projectionMatrix;
    renderView.cameraPosition = request.view.cameraPosition;
    renderView.width = request.view.width;
    renderView.height = request.view.height;
    renderView.showFaces = request.options.showSurfaces;
    renderView.showEdges = request.options.showEdges;
    renderView.showOverlay = request.options.showOverlays;
    renderView.showViewCube = request.options.showViewCube;

    RenderTargetInfo frameTargetInfo;
    frameTargetInfo.width = renderView.width;
    frameTargetInfo.height = renderView.height;
    frameTargetInfo.hasDepth = true;
    frameTargetInfo.presentable = surface.swapChain != nullptr;

    RenderFrame frame{device, *cmd, renderView, frameTargetInfo};
    executeStages(frame);

    cmd->endRenderPass();
    cmd->end();

    endFrame(device, surface);
}

void RenderRenderer::compile(const RenderRequest& request) {
    if (request.world) {
        workload_.build(*request.world, request.options);

        RenderCompileContext compileContext{
            .resources = *resources_,
            .textures = *texture_cache_,
            .materials = *material_cache_,
            .surfacePipeline = face_stage_ ? face_stage_->pipelineState() : nullptr,
            .edgePipeline = edge_stage_ ? edge_stage_->pipelineState() : nullptr,
        };
        compiler_.compile(*request.world, workload_, compileContext);
    } else {
        compiler_.clear();
    }

    const std::span<const MeshDrawCommand> emptyCommands;
    if (face_stage_) {
        face_stage_->setDrawCommands(request.options.showSurfaces
                                         ? compiler_.surfaceCommands()
                                         : emptyCommands);
    }
    if (edge_stage_) {
        edge_stage_->setDrawCommands(request.options.showEdges
                                         ? compiler_.edgeCommands()
                                         : emptyCommands);
    }
}

CommandList* RenderRenderer::beginFrame(RHIDevice& device,
                                        const RenderSurfaceBinding& surface,
                                        const RenderRequest& request) {
    device.beginFrame(surface.swapChain ? surface.swapChain : nullptr);
    auto* cmd = device.frameCommandList();
    if (!cmd) return nullptr;
    cmd->begin();

    if (surface.renderTarget) {
        cmd->beginRenderPass(surface.renderTarget->renderPassBeginInfo());
    } else {
        cmd->beginRenderPass(surface.swapChain->renderPassBeginInfo());
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
    if (face_stage_) face_stage_->execute(frame);
    if (edge_stage_) edge_stage_->execute(frame);
    if (face_stage_ || edge_stage_) {
        material_cache_->clearDirtyMaterials();
    }

    if (view_cube_stage_ && frame.view.showViewCube) {
        view_cube_stage_->setPipelines(face_stage_ ? face_stage_->pipelineState() : nullptr,
                                       edge_stage_ ? edge_stage_->pipelineState() : nullptr);
        view_cube_stage_->setFallbackResources(face_stage_ ? face_stage_->defaultWhiteTexture() : nullptr,
                                               face_stage_ ? face_stage_->defaultSampler() : nullptr);
        view_cube_stage_->execute(frame);
    }
}

void RenderRenderer::endFrame(RHIDevice& device, const RenderSurfaceBinding& surface) {
    if (surface.renderTarget) {
        device.submitOffscreen();
    } else {
        device.submitAndPresent(surface.swapChain);
    }
}

} // namespace mulan::engine
