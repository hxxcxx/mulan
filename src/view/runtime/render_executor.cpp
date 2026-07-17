#include "runtime/detail/render_executor.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/rhi/device.h>
#include <mulan/rhi/engine_error_code.h>

#include <string_view>
#include <utility>

namespace mulan::view::detail {
namespace {

Error executorError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

Error unavailableDeviceError() {
    return engine::makeError(engine::EngineErrorCode::DeviceLost, "Shared render device is no longer healthy.");
}

engine::DisplayMode toDisplayMode(RenderMode mode) {
    switch (mode) {
    case RenderMode::Shaded: return engine::DisplayMode::Shaded;
    case RenderMode::ShadedWithEdges: return engine::DisplayMode::ShadedWithEdges;
    case RenderMode::Wireframe: return engine::DisplayMode::Wireframe;
    }
    return engine::DisplayMode::ShadedWithEdges;
}

engine::SurfaceTechnique toSurfaceTechnique(SurfaceShading shading) {
    switch (shading) {
    case SurfaceShading::SolidLit: return engine::SurfaceTechnique::SolidLit;
    case SurfaceShading::SurfacePBR: return engine::SurfaceTechnique::SurfacePBR;
    }
    return engine::SurfaceTechnique::SolidLit;
}

engine::RenderSurfaceBinding bindSurface(RenderSurface& surface) {
    return engine::RenderSurfaceBinding{
        .swapChain = surface.swapChain(),
        .renderTarget = surface.renderTarget(),
    };
}

engine::RenderRequest buildRenderRequest(RenderSurface& surface, const RenderSubmission& submission) {
    const ViewState& viewState = submission.view;
    engine::RenderRequest request;
    request.sceneWorld = submission.sceneWorld.get();
    request.overlayWorld = submission.overlayWorld.get();
    request.view.viewMatrix = viewState.viewMatrix;
    request.view.projectionMatrix = viewState.projectionMatrix;
    request.view.cameraPosition = viewState.cameraPosition;
    request.view.width = static_cast<uint32_t>(viewState.width);
    request.view.height = static_cast<uint32_t>(viewState.height);
    request.output.mode = surface.isOffscreen() ? engine::RenderTargetMode::Capture : engine::RenderTargetMode::Present;
    request.output.width = request.view.width;
    request.output.height = request.view.height;
    request.output.readback = surface.isOffscreen();
    request.output.capture.width = request.output.width;
    request.output.capture.height = request.output.height;
    request.output.capture.format =
            surface.renderTarget() ? surface.renderTarget()->colorFormat() : surface.swapChain()->colorFormat();
    request.output.capture.depthFormat =
            surface.renderTarget() ? surface.renderTarget()->depthFormat() : surface.swapChain()->depthFormat();
    request.output.capture.readback = request.output.readback;
    request.options.displayMode = toDisplayMode(viewState.renderMode);
    request.options.surfaceTechnique = toSurfaceTechnique(viewState.surfaceShading);
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

RenderExecutor::~RenderExecutor() {
    shutdown();
}

ResultVoid RenderExecutor::initWindow(const ViewConfig& config, int width, int height) {
    auto context = RenderDeviceContext::create(config);
    if (!context) {
        LOG_ERROR("[RenderExecutor] Window initialization failed while creating device: {}", context.error().message);
        return std::unexpected(context.error());
    }
    return initWindow(std::move(*context), config, width, height);
}

ResultVoid RenderExecutor::initWindow(std::shared_ptr<RenderDeviceContext> context, const ViewConfig& config, int width,
                                      int height) {
    MULAN_PROFILE_ZONE();

    if (initialized_) {
        return {};
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(
                executorError(ErrorCode::InvalidArg, "Window render surface size must be greater than zero."));
    }
    if (!config.toNativeWindowHandle().valid()) {
        LOG_ERROR("[RenderExecutor] Window initialization rejected: invalid native window handle");
        return std::unexpected(
                executorError(ErrorCode::InvalidArg, "Window render session requires a valid native window."));
    }

    if (!context) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Window executor requires a device context."));
    }
    device_context_ = std::move(context);

    if (!device_context_->isHealthy()) {
        shutdownLocked();
        return std::unexpected(unavailableDeviceError());
    }
    auto& device = device_context_->device();
    if (auto surfaceInitialized = surface_.initWindowSurface(device, config, width, height); !surfaceInitialized) {
        shutdownLocked();
        LOG_ERROR("[RenderExecutor] Window surface initialization failed: size={}x{}, error={}", width, height,
                  surfaceInitialized.error().message);
        return std::unexpected(surfaceInitialized.error());
    }

    auto initialized = initRenderer();
    if (!initialized) {
        shutdownLocked();
        LOG_ERROR("[RenderExecutor] Renderer initialization failed: {}", initialized.error().message);
        return initialized;
    }

    initialized_ = true;
    LOG_INFO("[RenderExecutor] Window executor initialized: backend={}, size={}x{}", static_cast<int>(config.backend),
             width, height);
    return {};
}

ResultVoid RenderExecutor::initOffscreen(const ViewConfig& config, int width, int height) {
    auto context = RenderDeviceContext::create(config);
    if (!context) {
        LOG_ERROR("[RenderExecutor] Offscreen initialization failed while creating device: {}",
                  context.error().message);
        return std::unexpected(context.error());
    }
    return initOffscreen(std::move(*context), config, width, height);
}

ResultVoid RenderExecutor::initOffscreen(std::shared_ptr<RenderDeviceContext> context, const ViewConfig& config,
                                         int width, int height) {
    if (initialized_) {
        return {};
    }
    if (width <= 0 || height <= 0) {
        return std::unexpected(
                executorError(ErrorCode::InvalidArg, "Offscreen render surface size must be greater than zero."));
    }

    if (!context) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Offscreen executor requires a device context."));
    }
    device_context_ = std::move(context);

    if (!device_context_->isHealthy()) {
        shutdownLocked();
        return std::unexpected(unavailableDeviceError());
    }
    auto& device = device_context_->device();
    if (auto surfaceInitialized = surface_.initOffscreenSurface(device, width, height); !surfaceInitialized) {
        shutdownLocked();
        LOG_ERROR("[RenderExecutor] Offscreen surface initialization failed: size={}x{}, error={}", width, height,
                  surfaceInitialized.error().message);
        return std::unexpected(surfaceInitialized.error());
    }

    auto initialized = initRenderer();
    if (!initialized) {
        shutdownLocked();
        LOG_ERROR("[RenderExecutor] Renderer initialization failed: {}", initialized.error().message);
        return initialized;
    }

    initialized_ = true;
    LOG_INFO("[RenderExecutor] Offscreen executor initialized: backend={}, size={}x{}",
             static_cast<int>(config.backend), width, height);
    return {};
}

void RenderExecutor::shutdown() {
    shutdownLocked();
}

bool RenderExecutor::isInitialized() const {
    return initialized_;
}

RenderSurfaceState RenderExecutor::surfaceState() const {
    return surfaceStateLocked();
}

ResultVoid RenderExecutor::prepareResources(const engine::RenderResourcePrepareList& prepare) {
    if (!initialized_ || !device_context_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render session is not initialized."));
    }
    if (!device_context_->isHealthy()) {
        return std::unexpected(unavailableDeviceError());
    }

    auto prepared = renderer_.preparePersistentResources(resource_client_, prepare);
    if (!prepared && engine::isDeviceFatalError(prepared.error())) {
        device_context_->markFailed();
    }
    return prepared;
}

ResultVoid RenderExecutor::executeFrame(const RenderSubmission& submission) {
    if (!initialized_ || !device_context_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render session is not initialized."));
    }
    if (!device_context_->isHealthy()) {
        return std::unexpected(unavailableDeviceError());
    }

    const RenderSurfaceState state = surfaceStateLocked();
    if (!state.valid || (!surface_.renderTarget() && !surface_.swapChain())) {
        return std::unexpected(executorError(ErrorCode::Internal, "Render surface is not available."));
    }

    light_environment_ = submission.lightEnvironment;
    auto request = buildRenderRequest(surface_, submission);
    auto rendered = renderer_.render(device_context_->device(), bindSurface(surface_), request);
    if (!rendered && engine::isDeviceFatalError(rendered.error())) {
        device_context_->markFailed();
    }
    return rendered;
}

Result<engine::RenderCaptureResult> RenderExecutor::capture(const RenderSubmission& submission,
                                                            const engine::RenderCaptureDesc& desc) {
    if (!initialized_ || !device_context_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render session is not initialized."));
    }
    if (!device_context_->isHealthy()) {
        return std::unexpected(unavailableDeviceError());
    }

    const uint32_t width = desc.width ? desc.width : static_cast<uint32_t>(surface_.width());
    const uint32_t height = desc.height ? desc.height : static_cast<uint32_t>(surface_.height());
    if (width == 0 || height == 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Capture size must be greater than zero."));
    }

    if (auto configured = configureCaptureSurface(desc, width, height); !configured) {
        if (engine::isDeviceFatalError(configured.error()))
            device_context_->markFailed();
        return std::unexpected(configured.error());
    }

    light_environment_ = submission.lightEnvironment;
    auto request = buildRenderRequest(capture_surface_, submission);
    auto rendered = renderer_.render(device_context_->device(), bindSurface(capture_surface_), request);
    if (!rendered) {
        if (engine::isDeviceFatalError(rendered.error())) {
            device_context_->markFailed();
        }
        return std::unexpected(rendered.error());
    }

    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    // 截图目标匹配视图渲染器的目标签名；图像编码器负责处理 BGRA 等存储格式。
    result.format = capture_surface_.colorFormat(device_context_->device());
    result.bytesPerPixel = capture_surface_.bytesPerPixel();
    result.rowBytes = capture_surface_.rowBytes();
    if (desc.readback) {
        auto readback = capture_surface_.readbackPixels(device_context_->device(), result.pixels);
        if (!readback) {
            if (engine::isDeviceFatalError(readback.error()))
                device_context_->markFailed();
            return std::unexpected(readback.error());
        }
    }
    return result;
}

Result<RenderSurfaceState> RenderExecutor::resize(int width, int height) {
    if (!device_context_ || !initialized_ || width <= 0 || height <= 0) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Render surface resize is invalid."));
    }
    if (!device_context_->isHealthy()) {
        return std::unexpected(unavailableDeviceError());
    }

    if (auto resized = surface_.resize(device_context_->device(), width, height); !resized) {
        if (engine::isDeviceFatalError(resized.error())) {
            device_context_->markFailed();
        }
        return std::unexpected(resized.error());
    }
    return surfaceStateLocked();
}

void RenderExecutor::enableIBL(const std::string& hdrPath) {
    if (!device_context_ || !device_context_->isHealthy() || hdrPath.empty()) {
        return;
    }

    renderer_.enableIBL(device_context_->device(), hdrPath);
}

void RenderExecutor::clearAssetResources() {
    if (!device_context_ || !device_context_->isHealthy() || !renderer_.isInitialized()) {
        return;
    }

    if (resource_client_ != 0) {
        auto released = device_context_->resources().releaseClient(resource_client_);
        if (!released) {
            LOG_ERROR("[RenderExecutor] Device resource client release failed: {}", released.error().message);
        }
    }
    resource_client_ = device_context_->resources().registerClient();
}

ResultVoid RenderExecutor::initRenderer() {
    MULAN_PROFILE_ZONE();

    if (!device_context_) {
        return std::unexpected(
                executorError(ErrorCode::InvalidArg, "RenderExecutor cannot initialize without a device."));
    }

    auto& device = device_context_->device();
    if (resource_client_ == 0) {
        resource_client_ = device_context_->resources().registerClient();
    }
    return renderer_.init(device, device_context_->resources(), light_environment_, surface_.colorFormat(device),
                          surface_.depthFormat(device), surface_.sampleCount());
}

ResultVoid RenderExecutor::configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width,
                                                   uint32_t height) {
    if (!device_context_) {
        return std::unexpected(executorError(ErrorCode::InvalidArg, "Capture requires a device context."));
    }

    auto& device = device_context_->device();
    RenderSurfaceDesc captureDesc;
    captureDesc.width = static_cast<int>(width);
    captureDesc.height = static_cast<int>(height);
    // 保持截图表面与主表面管线签名兼容，以复用渲染器和资产 GPU 缓存。
    captureDesc.colorFormat = surface_.colorFormat(device);
    captureDesc.depthFormat = surface_.depthFormat(device);
    captureDesc.hasDepth = true;
    captureDesc.sampleCount = surface_.sampleCount();
    captureDesc.readback = desc.readback;
    return capture_surface_.configureOffscreenSurface(device, captureDesc);
}

RenderSurfaceState RenderExecutor::surfaceStateLocked() const {
    return RenderSurfaceState{
        .width = surface_.width() > 0 ? static_cast<uint32_t>(surface_.width()) : 0,
        .height = surface_.height() > 0 ? static_cast<uint32_t>(surface_.height()) : 0,
        .valid = surface_.isInitialized(),
    };
}

void RenderExecutor::shutdownLocked() {
    const bool hadResources = initialized_ || device_context_ != nullptr;
    if (device_context_) {
        auto& device = device_context_->device();
        // 先移除所有 Surface，确保执行域租约释放后再也没有窗口/截图后备缓冲引用 Device。
        capture_surface_.shutdown(device);
        surface_.shutdown(device);
        // 即使初始化只完成了一部分，也必须让 RenderRenderer 无条件清理已创建资源。
        renderer_.shutdown(device);
        if (resource_client_ != 0) {
            auto released = device_context_->resources().releaseClient(resource_client_);
            if (!released) {
                LOG_ERROR("[RenderExecutor] Device resource client shutdown failed: {}", released.error().message);
            }
            resource_client_ = 0;
        }
    }
    device_context_.reset();
    resource_client_ = 0;
    initialized_ = false;
    if (hadResources) {
        LOG_INFO("[RenderExecutor] Executor shut down");
    }
}

}  // namespace mulan::view::detail
