#include "render_runtime.h"

#include <mulan/engine/rhi/device.h>

#include <string_view>
#include <utility>

namespace mulan::view {
namespace {

core::Error runtimeError(core::ErrorCode code, std::string_view message) {
    return core::Error::make(code, message);
}

} // namespace

RenderRuntime::~RenderRuntime() {
    shutdown();
}

std::expected<void, core::Error>
RenderRuntime::initWindow(const ViewConfig& cfg,
                          int width,
                          int height,
                          engine::LightEnvironment& lightEnv) {
    if (initialized_) {
        return {};
    }

    engine::NativeWindowHandle window = cfg.toNativeWindowHandle();
    if (!window.valid()) {
        return std::unexpected(runtimeError(core::ErrorCode::InvalidArg,
                                           "Window render runtime requires a valid native window."));
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
        return std::unexpected(runtimeError(core::ErrorCode::Internal,
                                           "Failed to initialize window render surface."));
    }

    auto init = initRendering(lightEnv);
    if (!init) {
        shutdown();
        return init;
    }

    initialized_ = true;
    return {};
}

std::expected<void, core::Error>
RenderRuntime::initOffscreen(int width,
                             int height,
                             engine::LightEnvironment& lightEnv) {
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
        return std::unexpected(runtimeError(core::ErrorCode::Internal,
                                           "Failed to initialize offscreen render surface."));
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
    if (device_) {
        renderer_.shutdown(*device_);
        surface_.shutdown(*device_);
    }
    device_.reset();
    initialized_ = false;
}

void RenderRuntime::setRenderScene(const render_scene::RenderScene* scene,
                                   const asset::AssetLibrary* assets) {
    renderer_.setScene(scene, assets);
}

void RenderRuntime::render(const ViewState& viewState) {
    if (!initialized_ || !device_) {
        return;
    }
    renderer_.render(*device_, surface_, viewState);
}

void RenderRuntime::resize(int width, int height) {
    if (device_ && initialized_) {
        surface_.resize(*device_, width, height);
    }
}

void RenderRuntime::enableIBL(const std::string& hdrPath) {
    if (!device_ || hdrPath.empty()) {
        return;
    }
    renderer_.enableIBL(*device_, hdrPath);
}

bool RenderRuntime::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!device_) {
        return false;
    }
    return surface_.readbackPixels(*device_, pixels);
}

bool RenderRuntime::configureCaptureSurface(const engine::RenderCaptureDesc& desc,
                                            uint32_t width,
                                            uint32_t height) {
    if (!device_) {
        return false;
    }

    RenderSurfaceDesc surfaceDesc;
    surfaceDesc.width = static_cast<int>(width);
    surfaceDesc.height = static_cast<int>(height);
    surfaceDesc.colorFormat = desc.format;
    surfaceDesc.depthFormat = desc.depthFormat;
    surfaceDesc.hasDepth = true;
    surfaceDesc.readback = desc.readback;
    return surface_.configureOffscreenSurface(*device_, surfaceDesc);
}

std::expected<void, core::Error>
RenderRuntime::initRendering(engine::LightEnvironment& lightEnv) {
    if (!device_) {
        return std::unexpected(runtimeError(core::ErrorCode::InvalidArg,
                                           "RenderRuntime cannot initialize without a device."));
    }

    if (!renderer_.init(*device_,
                        lightEnv,
                        surface_.colorFormat(*device_),
                        surface_.depthFormat(*device_))) {
        return std::unexpected(runtimeError(core::ErrorCode::Internal,
                                           "Failed to initialize renderer."));
    }
    return {};
}

} // namespace mulan::view
