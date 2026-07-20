#include "detail/render_executor.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/rhi/device.h>

#include <string_view>
#include <utility>

namespace mulan::view::detail {
namespace {

Error executorError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

engine::DisplayMode toDisplayMode(RenderMode mode) {
    switch (mode) {
    case RenderMode::Shaded: return engine::DisplayMode::Shaded;
    case RenderMode::ShadedWithEdges: return engine::DisplayMode::ShadedWithEdges;
    case RenderMode::Wireframe: return engine::DisplayMode::Wireframe;
    }
    return engine::DisplayMode::ShadedWithEdges;
}

engine::RenderRequest buildRenderRequest(const RenderSubmission& submission) {
    const ViewState& viewState = submission.view;
    engine::RenderRequest request;
    request.sceneWorld = submission.sceneWorld.get();
    request.overlayWorld = submission.overlayWorld.get();
    request.view.viewMatrix = viewState.viewMatrix;
    request.view.projectionMatrix = viewState.projectionMatrix;
    request.view.cameraPosition = viewState.cameraPosition;
    request.view.width = static_cast<uint32_t>(viewState.width);
    request.view.height = static_cast<uint32_t>(viewState.height);
    request.options.displayMode = toDisplayMode(viewState.renderMode);
    request.options.hoveredPickId = viewState.hoveredPickId;
    request.options.selectionVisuals = viewState.selectionVisuals;
    request.options.showSurfaces = viewState.showFaces;
    request.options.showEdges = viewState.showEdges;
    request.options.showOverlays = viewState.showOverlays;
    request.options.showViewCube = viewState.showViewCube;
    request.options.viewCubeLayout = viewState.viewCubeLayout;
    request.options.viewCubeInteraction = viewState.viewCubeInteraction;
    return request;
}

}  // namespace

RenderExecutor::RenderExecutor(RenderDeviceContext& context)
    : device_context_(context), present_surface_(context.device()) {
}

RenderExecutor::~RenderExecutor() {
    shutdown();
}

ResultVoid RenderExecutor::init(const PresentSurfaceConfig& config, int width, int height) {
    MULAN_PROFILE_ZONE();

    if (initialized_) {
        return {};
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Present surface size must be greater than zero."));
    }
    if (!config.window.valid()) {
        LOG_ERROR("[RenderExecutor] Initialization rejected: invalid native window handle");
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Present surface requires a valid native window."));
    }

    if (auto surfaceInitialized = present_surface_.init(config, width, height); !surfaceInitialized) {
        shutdownResources();
        LOG_ERROR("[RenderExecutor] SwapChain initialization failed: size={}x{}, error={}", width, height,
                  surfaceInitialized.error().message);
        return std::unexpected(surfaceInitialized.error());
    }

    auto initialized = initRenderer();
    if (!initialized) {
        shutdownResources();
        LOG_ERROR("[RenderExecutor] Renderer initialization failed: {}", initialized.error().message);
        return initialized;
    }

    initialized_ = true;
    LOG_INFO("[RenderExecutor] Initialized: backend={}, size={}x{}",
             static_cast<int>(device_context_.device().backend()), width, height);
    return {};
}

void RenderExecutor::shutdown() {
    shutdownResources();
}

bool RenderExecutor::isInitialized() const {
    return initialized_;
}

PresentSurfaceState RenderExecutor::presentSurfaceState() const {
    return makePresentSurfaceState();
}

ResultVoid RenderExecutor::prepareResources(const engine::RenderResourcePrepareList& prepare) {
    if (!initialized_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render executor is not initialized."));
    }
    return device_context_.resources().preparePersistentResources(resource_client_, prepare);
}

ResultVoid RenderExecutor::executeFrame(const RenderSubmission& submission) {
    if (!initialized_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render executor is not initialized."));
    }
    const PresentSurfaceState state = makePresentSurfaceState();
    if (!state.valid) {
        return std::unexpected(executorError(ErrorCode::Internal, "Present surface is not available."));
    }

    auto request = buildRenderRequest(submission);
    return forward_renderer_.render(device_context_.device(), engine::RenderOutput(present_surface_.swapChain()),
                                    request, submission.lightEnvironment);
}

Result<engine::RenderCaptureResult> RenderExecutor::capture(const RenderSubmission& submission,
                                                            const engine::RenderCaptureDesc& desc) {
    if (!initialized_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render executor is not initialized."));
    }
    const uint32_t width = desc.width ? desc.width : static_cast<uint32_t>(present_surface_.width());
    const uint32_t height = desc.height ? desc.height : static_cast<uint32_t>(present_surface_.height());
    if (width == 0 || height == 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Capture size must be greater than zero."));
    }

    if (auto configured = configureCaptureTarget(desc, width, height); !configured) {
        return std::unexpected(configured.error());
    }

    auto request = buildRenderRequest(submission);
    auto rendered =
            forward_renderer_.render(device_context_.device(), engine::RenderOutput(capture_target_->renderTarget()),
                                     request, submission.lightEnvironment);
    if (!rendered) {
        return std::unexpected(rendered.error());
    }

    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    // 截图目标匹配视图渲染器的目标签名；图像编码器负责处理 BGRA 等存储格式。
    result.format = capture_target_->colorFormat();
    result.bytesPerPixel = capture_target_->bytesPerPixel();
    result.rowBytes = capture_target_->rowBytes();
    if (desc.readback) {
        auto readback = capture_target_->readbackPixels(result.pixels);
        if (!readback) {
            return std::unexpected(readback.error());
        }
    }
    return result;
}

Result<PresentSurfaceState> RenderExecutor::resize(int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Present surface resize is invalid."));
    }
    if (auto resized = present_surface_.resize(width, height); !resized) {
        return std::unexpected(resized.error());
    }
    return makePresentSurfaceState();
}

void RenderExecutor::enableIBL(const std::string& hdrPath) {
    if (hdrPath.empty()) {
        return;
    }

    forward_renderer_.enableIBL(device_context_.device(), hdrPath);
}

ResultVoid RenderExecutor::clearAssetResources() {
    if (!forward_renderer_.isInitialized()) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render executor is not initialized."));
    }
    if (resource_client_ != 0) {
        const engine::DeviceResourceClientId releasedClient = std::exchange(resource_client_, 0);
        auto released = device_context_.resources().releaseClient(releasedClient);
        if (!released) {
            return std::unexpected(released.error());
        }
    }
    resource_client_ = device_context_.resources().registerClient();
    return {};
}

ResultVoid RenderExecutor::initRenderer() {
    MULAN_PROFILE_ZONE();

    auto& device = device_context_.device();
    if (resource_client_ == 0) {
        resource_client_ = device_context_.resources().registerClient();
    }
    const engine::RenderTargetInfo target{
        .colorFormat = present_surface_.colorFormat(),
        .depthFormat = present_surface_.hasDepth() ? present_surface_.depthFormat() : engine::TextureFormat::Unknown,
        .hasDepth = present_surface_.hasDepth(),
        .sampleCount = present_surface_.sampleCount(),
    };
    return forward_renderer_.init(device, device_context_.resources(), target);
}

ResultVoid RenderExecutor::configureCaptureTarget(const engine::RenderCaptureDesc& desc, uint32_t width,
                                                  uint32_t height) {
    CaptureTargetDesc captureDesc;
    captureDesc.width = static_cast<int>(width);
    captureDesc.height = static_cast<int>(height);
    // 保持截图表面与主表面管线签名兼容，以复用渲染器和资产 GPU 缓存。
    captureDesc.colorFormat = present_surface_.colorFormat();
    captureDesc.depthFormat = present_surface_.depthFormat();
    captureDesc.hasDepth = present_surface_.hasDepth();
    captureDesc.sampleCount = present_surface_.sampleCount();
    captureDesc.readback = desc.readback;
    if (!capture_target_)
        capture_target_.emplace(device_context_.device());
    return capture_target_->configure(captureDesc);
}

PresentSurfaceState RenderExecutor::makePresentSurfaceState() const {
    return PresentSurfaceState{
        .width = present_surface_.width() > 0 ? static_cast<uint32_t>(present_surface_.width()) : 0,
        .height = present_surface_.height() > 0 ? static_cast<uint32_t>(present_surface_.height()) : 0,
        .valid = present_surface_.isInitialized(),
    };
}

void RenderExecutor::shutdownResources() {
    const bool hadResources = initialized_ || present_surface_.isInitialized() ||
                              (capture_target_ && capture_target_->isConfigured()) ||
                              forward_renderer_.isInitialized() || resource_client_ != 0;
    auto& device = device_context_.device();
    if (capture_target_) {
        capture_target_->shutdown();
        capture_target_.reset();
    }
    present_surface_.shutdown();
    // 即使初始化只完成了一部分，也必须让 ForwardRenderer 无条件清理已创建资源。
    forward_renderer_.shutdown(device);
    if (resource_client_ != 0) {
        auto released = device_context_.resources().releaseClient(resource_client_);
        if (!released) {
            LOG_ERROR("[RenderExecutor] Device resource client shutdown failed: {}", released.error().message);
        }
        resource_client_ = 0;
    }
    initialized_ = false;
    if (hadResources) {
        LOG_INFO("[RenderExecutor] Executor shut down");
    }
}

}  // namespace mulan::view::detail
