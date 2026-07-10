#include "render_runtime.h"

#include <mulan/engine/rhi/device.h>

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

core::Result<void> RenderRuntime::initWindow(const ViewConfig& cfg, int width, int height,
                                             engine::LightEnvironment& lightEnv) {
    if (initialized_) {
        return {};
    }

    engine::NativeWindowHandle window = cfg.toNativeWindowHandle();
    if (!window.valid()) {
        return std::unexpected(
                runtimeError(core::ErrorCode::InvalidArg, "Window render runtime requires a valid native window."));
    }

    engine::DeviceCreateInfo ci;
    ci.backend = cfg.backend;
    ci.window = window;
    ci.renderConfig = cfg.toRenderConfig();
    ci.enableValidation = cfg.enableValidation;

    auto device = engine::RHIDevice::create(ci);
    if (!device) {
        return std::unexpected(device.error());
    }
    device_ = std::move(*device);

    if (!surface_.initWindowSurface(*device_, cfg, width, height)) {
        shutdown();
        return std::unexpected(runtimeError(core::ErrorCode::Internal, "Failed to initialize window render surface."));
    }

    auto init = initRendering(lightEnv);
    if (!init) {
        shutdown();
        return init;
    }

    initialized_ = true;
    return {};
}

core::Result<void> RenderRuntime::initOffscreen(int width, int height, engine::LightEnvironment& lightEnv) {
    if (initialized_) {
        return {};
    }

    engine::RenderConfig config;
    config.bufferCount = 2;
    config.vsync = false;
    config.depthBuffer = true;
    config.stencilBuffer = false;

    engine::DeviceCreateInfo ci;
    ci.backend = engine::GraphicsBackend::Vulkan;
    ci.window = {};
    ci.renderConfig = config;
    ci.enableValidation = true;

    auto device = engine::RHIDevice::create(ci);
    if (!device) {
        return std::unexpected(device.error());
    }
    device_ = std::move(*device);

    if (!surface_.initOffscreenSurface(*device_, width, height)) {
        shutdown();
        return std::unexpected(
                runtimeError(core::ErrorCode::Internal, "Failed to initialize offscreen render surface."));
    }

    auto init = initRendering(lightEnv);
    if (!init) {
        shutdown();
        return init;
    }

    initialized_ = true;
    return {};
}

void RenderRuntime::shutdown() {
    execute(ShutdownRendererCommand{});
}

RenderRuntimeCommandResult RenderRuntime::execute(RenderRuntimeCommand command) {
    return std::visit(Overloaded{
                              [this](const ResizeSurfaceCommand& resize) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_ || !initialized_ || resize.width <= 0 || resize.height <= 0) {
                                      return result;
                                  }
                                  surface_.resize(*device_, resize.width, resize.height);
                                  result.succeeded = true;
                                  return result;
                              },
                              [this](const EnableIblCommand& ibl) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_ || ibl.hdrPath.empty()) {
                                      return result;
                                  }
                                  renderer_.enableIBL(*device_, ibl.hdrPath);
                                  result.succeeded = true;
                                  return result;
                              },
                              [this](const ConfigureCaptureSurfaceCommand& configure) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_ || configure.width == 0 || configure.height == 0) {
                                      return result;
                                  }

                                  RenderSurfaceDesc surfaceDesc;
                                  surfaceDesc.width = static_cast<int>(configure.width);
                                  surfaceDesc.height = static_cast<int>(configure.height);
                                  surfaceDesc.colorFormat = configure.capture.format;
                                  surfaceDesc.depthFormat = configure.capture.depthFormat;
                                  surfaceDesc.hasDepth = true;
                                  surfaceDesc.sampleCount = configure.capture.sampleCount
                                                                    ? configure.capture.sampleCount
                                                                    : surface_.sampleCount();
                                  surfaceDesc.readback = configure.capture.readback;
                                  result.succeeded = surface_.configureOffscreenSurface(*device_, surfaceDesc);
                                  return result;
                              },
                              [this](const ConfigureOffscreenSurfaceCommand& configure) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_) {
                                      return result;
                                  }
                                  result.succeeded = surface_.configureOffscreenSurface(*device_, configure.surface);
                                  return result;
                              },
                              [this](const ReadbackPixelsCommand&) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_) {
                                      return result;
                                  }
                                  result.succeeded = surface_.readbackPixels(*device_, result.pixels);
                                  if (!result.succeeded) {
                                      result.pixels.clear();
                                  }
                                  return result;
                              },
                              [this](const ClearAssetResourcesCommand&) {
                                  RenderRuntimeCommandResult result;
                                  if (!device_) {
                                      return result;
                                  }
                                  renderer_.clearAssetResources(*device_);
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
    if (device_) {
        renderer_.shutdown(*device_);
        surface_.shutdown(*device_);
    }
    device_.reset();
    asset_source_ = nullptr;
    submission_builder_.setScene(nullptr, nullptr);
    submission_builder_.setPreviewLayer(nullptr);
    initialized_ = false;
}

void RenderRuntime::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    if (assets != asset_source_) {
        execute(ClearAssetResourcesCommand{});
    }
    asset_source_ = assets;
    submission_builder_.setScene(scene, assets);
}

void RenderRuntime::setPreviewLayer(const PreviewLayer* preview) {
    submission_builder_.setPreviewLayer(preview);
}

void RenderRuntime::render(const ViewState& viewState) {
    if (!initialized_ || !device_) {
        return;
    }
    renderer_.render(*device_, surface_, submission_builder_.build(viewState));
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
    return surface_.offscreenDesc();
}

core::Result<void> RenderRuntime::initRendering(engine::LightEnvironment& lightEnv) {
    if (!device_) {
        return std::unexpected(
                runtimeError(core::ErrorCode::InvalidArg, "RenderRuntime cannot initialize without a device."));
    }

    if (!renderer_.init(*device_, lightEnv, surface_.colorFormat(*device_), surface_.depthFormat(*device_),
                        surface_.sampleCount())) {
        return std::unexpected(runtimeError(core::ErrorCode::Internal, "Failed to initialize renderer."));
    }
    return {};
}

}  // namespace mulan::view
