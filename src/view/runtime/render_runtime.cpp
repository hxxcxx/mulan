#include <mulan/view/runtime/render_runtime.h>

#include <mulan/core/log/log.h>

#include <string_view>
#include <utility>
#include <variant>

namespace mulan::view {
namespace {

core::Error runtimeError(core::ErrorCode code, std::string_view message) {
    return core::Error::make(code, message);
}

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

}  // namespace

RenderRuntime::~RenderRuntime() {
    shutdown();
}

core::Result<void> RenderRuntime::initWindow(const ViewConfig& config, int width, int height) {
    std::scoped_lock runtimeLock(runtime_mutex_);
    if (initialized_) {
        return {};
    }
    if (!config.toNativeWindowHandle().valid()) {
        LOG_ERROR("[ViewRuntime] Window initialization rejected: invalid native window handle");
        return std::unexpected(
                runtimeError(core::ErrorCode::InvalidArg, "Window render runtime requires a valid native window."));
    }

    auto context = RenderDeviceContext::acquire(config);
    if (!context) {
        LOG_ERROR("[ViewRuntime] Window initialization failed while acquiring device: {}", context.error().message);
        return std::unexpected(context.error());
    }
    device_context_ = std::move(*context);

    auto deviceLock = device_context_->lock();
    auto& device = device_context_->device();
    if (!surface_.initWindowSurface(device, config, width, height)) {
        deviceLock.unlock();
        shutdownNow();
        LOG_ERROR("[ViewRuntime] Window surface initialization failed: size={}x{}", width, height);
        return std::unexpected(runtimeError(core::ErrorCode::Internal, "Failed to initialize window render surface."));
    }

    auto initialized = initRendering();
    if (!initialized) {
        deviceLock.unlock();
        shutdownNow();
        LOG_ERROR("[ViewRuntime] Window renderer initialization failed: {}", initialized.error().message);
        return initialized;
    }

    initialized_ = true;
    LOG_INFO("[ViewRuntime] Window runtime initialized: backend={}, size={}x{}, executionMode={}",
             static_cast<int>(config.backend), width, height, static_cast<int>(config.executionMode));
    return {};
}

core::Result<void> RenderRuntime::initOffscreen(const ViewConfig& config, int width, int height) {
    std::scoped_lock runtimeLock(runtime_mutex_);
    if (initialized_) {
        return {};
    }

    auto context = RenderDeviceContext::acquire(config);
    if (!context) {
        LOG_ERROR("[ViewRuntime] Offscreen initialization failed while acquiring device: {}", context.error().message);
        return std::unexpected(context.error());
    }
    device_context_ = std::move(*context);

    auto deviceLock = device_context_->lock();
    auto& device = device_context_->device();
    if (!surface_.initOffscreenSurface(device, width, height)) {
        deviceLock.unlock();
        shutdownNow();
        LOG_ERROR("[ViewRuntime] Offscreen surface initialization failed: size={}x{}", width, height);
        return std::unexpected(
                runtimeError(core::ErrorCode::Internal, "Failed to initialize offscreen render surface."));
    }

    auto initialized = initRendering();
    if (!initialized) {
        deviceLock.unlock();
        shutdownNow();
        LOG_ERROR("[ViewRuntime] Offscreen renderer initialization failed: {}", initialized.error().message);
        return initialized;
    }

    initialized_ = true;
    LOG_INFO("[ViewRuntime] Offscreen runtime initialized: backend={}, size={}x{}, executionMode={}",
             static_cast<int>(config.backend), width, height, static_cast<int>(config.executionMode));
    return {};
}

core::Result<void> RenderRuntime::initOffscreen(int width, int height) {
    ViewConfig config;
    config.vsync = false;
    return initOffscreen(config, width, height);
}

void RenderRuntime::shutdown() {
    execute(ShutdownRendererCommand{});
}

RenderRuntimeCommandResult RenderRuntime::execute(RenderRuntimeCommand command) {
    std::scoped_lock runtimeLock(runtime_mutex_);
    return std::visit(Overloaded{
                              [this](const ResizeSurfaceCommand& resize) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_ || !initialized_ || resize.width <= 0 || resize.height <= 0) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  surface_.resize(device_context_->device(), resize.width, resize.height);
                                  result.succeeded = true;
                                  return result;
                              },
                              [this](const EnableIblCommand& ibl) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_ || ibl.hdrPath.empty()) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  renderer_.enableIBL(device_context_->device(), ibl.hdrPath);
                                  result.succeeded = true;
                                  return result;
                              },
                              [this](const ConfigureCaptureSurfaceCommand& configure) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_ || configure.width == 0 || configure.height == 0) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  result.succeeded = configureDedicatedCaptureSurface(
                                          configure.capture, configure.width, configure.height);
                                  return result;
                              },
                              [this](const ConfigureOffscreenSurfaceCommand& configure) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_ || !surface_.isOffscreen()) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  result.succeeded = surface_.configureOffscreenSurface(device_context_->device(),
                                                                                        configure.surface);
                                  return result;
                              },
                              [this](const ReadbackPixelsCommand&) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  RenderSurface& target = surface_.isOffscreen() ? surface_ : capture_surface_;
                                  result.succeeded = target.readbackPixels(device_context_->device(), result.pixels);
                                  if (!result.succeeded) {
                                      result.pixels.clear();
                                  }
                                  return result;
                              },
                              [this](const ClearAssetResourcesCommand&) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_context_) {
                                      return result;
                                  }
                                  auto deviceLock = device_context_->lock();
                                  renderer_.clearAssetResources(device_context_->device());
                                  result.succeeded = true;
                                  return result;
                              },
                              [this](const ShutdownRendererCommand&) {
                                  shutdownNow();
                                  return RenderRuntimeCommandResult{ .succeeded = true };
                              },
                      },
                      std::move(command));
}

void RenderRuntime::shutdownNow() {
    const bool wasInitialized = initialized_ || device_context_ != nullptr;
    if (device_context_) {
        auto deviceLock = device_context_->lock();
        auto& device = device_context_->device();
        renderer_.shutdown(device);
        capture_surface_.shutdown(device);
        surface_.shutdown(device);
    }
    device_context_.reset();
    initialized_ = false;
    if (wasInitialized) {
        LOG_INFO("[ViewRuntime] Runtime shut down");
    }
}

void RenderRuntime::render(const RenderSubmission& submission) {
    std::scoped_lock runtimeLock(runtime_mutex_);
    if (!initialized_ || !device_context_) {
        return;
    }
    auto deviceLock = device_context_->lock();
    light_environment_ = submission.lightEnvironment;
    renderer_.render(device_context_->device(), surface_, submission);
}

core::Result<engine::RenderCaptureResult> RenderRuntime::capture(const RenderSubmission& submission,
                                                                 const engine::RenderCaptureDesc& desc) {
    std::scoped_lock runtimeLock(runtime_mutex_);
    if (!initialized_ || !device_context_) {
        return std::unexpected(runtimeError(core::ErrorCode::InvalidArg, "Render runtime is not initialized."));
    }

    const uint32_t width = desc.width ? desc.width : static_cast<uint32_t>(surface_.width());
    const uint32_t height = desc.height ? desc.height : static_cast<uint32_t>(surface_.height());
    if (width == 0 || height == 0) {
        return std::unexpected(runtimeError(core::ErrorCode::InvalidArg, "Capture size must be greater than zero."));
    }

    auto deviceLock = device_context_->lock();
    if (!configureDedicatedCaptureSurface(desc, width, height)) {
        return std::unexpected(
                runtimeError(core::ErrorCode::Internal, "Failed to configure dedicated capture surface."));
    }

    light_environment_ = submission.lightEnvironment;
    renderer_.render(device_context_->device(), capture_surface_, submission);

    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    // 截图目标刻意匹配渲染器的目标签名（窗口视图通常为 BGRA8）；
    // 图像编码器负责安全转换 BGRA。
    result.format = capture_surface_.colorFormat(device_context_->device());
    result.bytesPerPixel = capture_surface_.bytesPerPixel();
    result.rowBytes = capture_surface_.rowBytes();
    if (desc.readback && !capture_surface_.readbackPixels(device_context_->device(), result.pixels)) {
        return std::unexpected(runtimeError(core::ErrorCode::Io, "Capture readback failed."));
    }
    return result;
}

void RenderRuntime::resize(int width, int height) {
    execute(ResizeSurfaceCommand{ .width = width, .height = height });
}

void RenderRuntime::enableIBL(const std::string& hdrPath) {
    execute(EnableIblCommand{ .hdrPath = hdrPath });
}

bool RenderRuntime::readbackPixels(std::vector<uint8_t>& pixels) {
    auto result = execute(ReadbackPixelsCommand{});
    if (!result.succeeded) {
        return false;
    }
    pixels = std::move(result.pixels);
    return true;
}

bool RenderRuntime::configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height) {
    return execute(ConfigureCaptureSurfaceCommand{
                           .capture = desc,
                           .width = width,
                           .height = height,
                   })
            .succeeded;
}

bool RenderRuntime::configureOffscreenSurface(const RenderSurfaceDesc& desc) {
    return execute(ConfigureOffscreenSurfaceCommand{ .surface = desc }).succeeded;
}

std::optional<RenderSurfaceDesc> RenderRuntime::offscreenSurfaceDesc() const {
    std::scoped_lock runtimeLock(runtime_mutex_);
    return surface_.offscreenDesc();
}

core::Result<void> RenderRuntime::initRendering() {
    if (!device_context_) {
        return std::unexpected(
                runtimeError(core::ErrorCode::InvalidArg, "RenderRuntime cannot initialize without a device."));
    }
    auto& device = device_context_->device();
    if (!renderer_.init(device, light_environment_, surface_.colorFormat(device), surface_.depthFormat(device),
                        surface_.sampleCount())) {
        return std::unexpected(runtimeError(core::ErrorCode::Internal, "Failed to initialize renderer."));
    }
    return {};
}

bool RenderRuntime::configureDedicatedCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width,
                                                     uint32_t height) {
    if (!device_context_) {
        return false;
    }

    auto& device = device_context_->device();
    RenderSurfaceDesc captureDesc;
    captureDesc.width = static_cast<int>(width);
    captureDesc.height = static_cast<int>(height);
    // 渲染器的 PSO 由视图目标签名决定。保持截图表面兼容后，窗口与截图渲染
    // 可复用同一渲染器和 GPU 资产缓存，无需重新创建管线。
    captureDesc.colorFormat = surface_.colorFormat(device);
    captureDesc.depthFormat = surface_.depthFormat(device);
    captureDesc.hasDepth = true;
    captureDesc.sampleCount = surface_.sampleCount();
    captureDesc.readback = desc.readback;
    return capture_surface_.configureOffscreenSurface(device, captureDesc);
}

}  // namespace mulan::view
