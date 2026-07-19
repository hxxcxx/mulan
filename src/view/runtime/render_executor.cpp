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

engine::RenderSurfaceBinding bindSurface(RenderSurface& surface) {
    return engine::RenderSurfaceBinding{
        .swapChain = surface.swapChain(),
        .renderTarget = surface.renderTarget(),
    };
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

RenderExecutor::RenderExecutor(RenderDeviceContext& context) : device_context_(context) {
}

RenderExecutor::~RenderExecutor() {
    shutdown();
}

ResultVoid RenderExecutor::init(const RenderSurfaceConfig& config, int width, int height) {
    MULAN_PROFILE_ZONE();

    if (initialized_) {
        return {};
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render surface size must be greater than zero."));
    }
    if (!config.window.valid()) {
        LOG_ERROR("[RenderExecutor] Initialization rejected: invalid native window handle");
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render surface requires a valid native window."));
    }

    auto& device = device_context_.device();
    if (auto surfaceInitialized = surface_.initSwapChain(device, config, width, height); !surfaceInitialized) {
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

RenderSurfaceState RenderExecutor::surfaceState() const {
    return makeSurfaceState();
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
    const RenderSurfaceState state = makeSurfaceState();
    if (!state.valid || (!surface_.renderTarget() && !surface_.swapChain())) {
        return std::unexpected(executorError(ErrorCode::Internal, "Render surface is not available."));
    }

    auto request = buildRenderRequest(submission);
    return forward_renderer_.render(device_context_.device(), bindSurface(surface_), request,
                                    submission.lightEnvironment);
}

Result<engine::RenderCaptureResult> RenderExecutor::capture(const RenderSubmission& submission,
                                                            const engine::RenderCaptureDesc& desc) {
    if (!initialized_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render executor is not initialized."));
    }
    const uint32_t width = desc.width ? desc.width : static_cast<uint32_t>(surface_.width());
    const uint32_t height = desc.height ? desc.height : static_cast<uint32_t>(surface_.height());
    if (width == 0 || height == 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Capture size must be greater than zero."));
    }

    if (auto configured = configureCaptureSurface(desc, width, height); !configured) {
        return std::unexpected(configured.error());
    }

    auto request = buildRenderRequest(submission);
    auto rendered = forward_renderer_.render(device_context_.device(), bindSurface(capture_surface_), request,
                                             submission.lightEnvironment);
    if (!rendered) {
        return std::unexpected(rendered.error());
    }

    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    // 截图目标匹配视图渲染器的目标签名；图像编码器负责处理 BGRA 等存储格式。
    result.format = capture_surface_.colorFormat();
    result.bytesPerPixel = capture_surface_.bytesPerPixel();
    result.rowBytes = capture_surface_.rowBytes();
    if (desc.readback) {
        auto readback = capture_surface_.readbackPixels(device_context_.device(), result.pixels);
        if (!readback) {
            return std::unexpected(readback.error());
        }
    }
    return result;
}

Result<RenderSurfaceState> RenderExecutor::resize(int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render surface resize is invalid."));
    }
    if (auto resized = surface_.resize(device_context_.device(), width, height); !resized) {
        return std::unexpected(resized.error());
    }
    return makeSurfaceState();
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
        .colorFormat = surface_.colorFormat(),
        .depthFormat = surface_.hasDepth() ? surface_.depthFormat() : engine::TextureFormat::Unknown,
        .hasDepth = surface_.hasDepth(),
        .sampleCount = surface_.sampleCount(),
    };
    return forward_renderer_.init(device, device_context_.resources(), target);
}

ResultVoid RenderExecutor::configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width,
                                                   uint32_t height) {
    auto& device = device_context_.device();
    RenderSurfaceDesc captureDesc;
    captureDesc.width = static_cast<int>(width);
    captureDesc.height = static_cast<int>(height);
    // 保持截图表面与主表面管线签名兼容，以复用渲染器和资产 GPU 缓存。
    captureDesc.colorFormat = surface_.colorFormat();
    captureDesc.depthFormat = surface_.depthFormat();
    captureDesc.hasDepth = surface_.hasDepth();
    captureDesc.sampleCount = surface_.sampleCount();
    captureDesc.readback = desc.readback;
    return capture_surface_.configureOffscreenSurface(device, captureDesc);
}

RenderSurfaceState RenderExecutor::makeSurfaceState() const {
    return RenderSurfaceState{
        .width = surface_.width() > 0 ? static_cast<uint32_t>(surface_.width()) : 0,
        .height = surface_.height() > 0 ? static_cast<uint32_t>(surface_.height()) : 0,
        .valid = surface_.isInitialized(),
    };
}

void RenderExecutor::shutdownResources() {
    const bool hadResources =
            initialized_ || surface_.isInitialized() || forward_renderer_.isInitialized() || resource_client_ != 0;
    auto& device = device_context_.device();
    // 先移除所有 Surface，确保通道释放后再也没有窗口/截图后备缓冲引用 Device。
    capture_surface_.shutdown(device);
    surface_.shutdown(device);
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
