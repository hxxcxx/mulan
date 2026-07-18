#include "detail/render_device_context.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>

namespace mulan::view::detail {

RenderDeviceContext::RenderDeviceContext(std::unique_ptr<engine::RHIDevice> device) : device_(std::move(device)) {
    MULAN_PROFILE_ZONE();
    resource_service_ = std::make_unique<engine::DeviceResourceService>(*device_);
}

Result<std::unique_ptr<RenderDeviceContext>> RenderDeviceContext::create(const ViewConfig& config) {
    MULAN_PROFILE_ZONE();

    const engine::RenderConfig renderConfig = config.toRenderConfig();

    engine::DeviceCreateInfo createInfo;
    createInfo.backend = config.backend;
    if (config.backend == engine::GraphicsBackend::OpenGL)
        createInfo.window = config.window;
    createInfo.renderConfig = renderConfig;
    createInfo.enableValidation = config.enableValidation;

    auto device = engine::RHIDevice::create(createInfo);
    if (!device) {
        LOG_ERROR("[RenderDeviceContext] Device creation failed: backend={}, validation={}, error={}",
                  static_cast<int>(config.backend), config.enableValidation, device.error().message);
        return std::unexpected(device.error());
    }

    auto context = std::unique_ptr<RenderDeviceContext>(new RenderDeviceContext(std::move(*device)));
    auto initialized = context->resource_service_->init();
    if (!initialized) {
        LOG_ERROR("[RenderDeviceContext] Device resource service initialization failed: {}",
                  initialized.error().message);
        return std::unexpected(initialized.error());
    }
    LOG_INFO("[RenderDeviceContext] Device context created: backend={}, validation={}",
             static_cast<int>(config.backend), config.enableValidation);
    return context;
}

}  // namespace mulan::view::detail
