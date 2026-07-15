#include "runtime/detail/render_device_context.h"

#include <mulan/core/log/log.h>

#include <algorithm>
#include <vector>

namespace mulan::view::detail {
namespace {

std::mutex registry_mutex;
std::vector<std::weak_ptr<RenderDeviceContext>> registry;

bool sameRenderConfig(const engine::RenderConfig& lhs, const engine::RenderConfig& rhs) {
    return lhs.msaa == rhs.msaa && lhs.bufferCount == rhs.bufferCount && lhs.vsync == rhs.vsync &&
           lhs.depthBuffer == rhs.depthBuffer && lhs.stencilBuffer == rhs.stencilBuffer &&
           lhs.clearDepth == rhs.clearDepth &&
           std::equal(std::begin(lhs.clearColor), std::end(lhs.clearColor), std::begin(rhs.clearColor));
}

}  // namespace

bool RenderDeviceContext::canShare(const ViewConfig& config) {
    // OpenGL 资源绑定到原生上下文；共享需要专门的共享上下文实现，不能在此隐式复用。
    // 同步调试模式不经过设备级调度器，也必须使用独立 Device，避免与 GPU 线程竞争。
    return config.backend != engine::GraphicsBackend::OpenGL && config.executionMode == RenderExecutionMode::Threaded;
}

bool RenderDeviceContext::matches(const RenderDeviceContext& context, const ViewConfig& config) {
    return context.backend_ == config.backend && context.validation_enabled_ == config.enableValidation &&
           sameRenderConfig(context.render_config_, config.toRenderConfig());
}

void RenderDeviceContext::markFailed() {
    if (healthy_.exchange(false)) {
        LOG_CRITICAL("[RenderDeviceContext] Shared device context marked unhealthy: backend={}",
                     static_cast<int>(backend_));
    }
}

Result<std::shared_ptr<RenderDeviceContext>> RenderDeviceContext::acquire(const ViewConfig& config) {
    const engine::RenderConfig renderConfig = config.toRenderConfig();
    std::scoped_lock registryLock(registry_mutex);

    if (canShare(config)) {
        for (auto it = registry.begin(); it != registry.end();) {
            if (auto existing = it->lock()) {
                if (!existing->isHealthy()) {
                    it = registry.erase(it);
                    continue;
                }
                if (matches(*existing, config)) {
                    LOG_DEBUG("[RenderDeviceContext] Reusing device context: backend={}",
                              static_cast<int>(config.backend));
                    return existing;
                }
                ++it;
            } else {
                it = registry.erase(it);
            }
        }
    }

    engine::DeviceCreateInfo createInfo;
    createInfo.backend = config.backend;
    createInfo.window = config.backend == engine::GraphicsBackend::OpenGL ? config.toNativeWindowHandle()
                                                                          : engine::NativeWindowHandle{};
    createInfo.renderConfig = renderConfig;
    createInfo.enableValidation = config.enableValidation;

    auto device = engine::RHIDevice::create(createInfo);
    if (!device) {
        LOG_ERROR("[RenderDeviceContext] Device creation failed: backend={}, validation={}, error={}",
                  static_cast<int>(config.backend), config.enableValidation, device.error().message);
        return std::unexpected(device.error());
    }

    auto context = std::shared_ptr<RenderDeviceContext>(new RenderDeviceContext(std::move(*device), config.backend));
    if (!context->resource_service_->init()) {
        return std::unexpected(Error::make(ErrorCode::Internal, "Failed to initialize device resource service."));
    }
    context->render_config_ = renderConfig;
    context->validation_enabled_ = config.enableValidation;
    if (canShare(config)) {
        registry.emplace_back(context);
    }
    LOG_INFO("[RenderDeviceContext] Device context created: backend={}, validation={}, shared={}",
             static_cast<int>(config.backend), config.enableValidation, canShare(config));
    return context;
}

}  // namespace mulan::view::detail
