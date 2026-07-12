#include <mulan/view/runtime/render_device_context.h>

#include <algorithm>
#include <vector>

namespace mulan::view {
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

bool RenderDeviceContext::canShare(engine::GraphicsBackend backend) {
    // OpenGL 资源绑定到原生上下文；共享需要专门的共享上下文实现，不能在此隐式复用。
    return backend != engine::GraphicsBackend::OpenGL;
}

bool RenderDeviceContext::matches(const RenderDeviceContext& context, const ViewConfig& config) {
    return context.backend_ == config.backend && context.validation_enabled_ == config.enableValidation &&
           sameRenderConfig(context.render_config_, config.toRenderConfig());
}

core::Result<std::shared_ptr<RenderDeviceContext>> RenderDeviceContext::acquire(const ViewConfig& config) {
    const engine::RenderConfig renderConfig = config.toRenderConfig();
    std::scoped_lock registryLock(registry_mutex);

    if (canShare(config.backend)) {
        for (auto it = registry.begin(); it != registry.end();) {
            if (auto existing = it->lock()) {
                if (matches(*existing, config)) {
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
        return std::unexpected(device.error());
    }

    auto context = std::shared_ptr<RenderDeviceContext>(new RenderDeviceContext(std::move(*device), config.backend));
    context->render_config_ = renderConfig;
    context->validation_enabled_ = config.enableValidation;
    if (canShare(config.backend)) {
        registry.emplace_back(context);
    }
    return context;
}

}  // namespace mulan::view
