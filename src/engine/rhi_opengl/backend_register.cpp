#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "detail/gl_device.h"

namespace mulan::engine {
namespace {

core::Result<std::unique_ptr<RHIDevice>> createOpenGLDevice(const DeviceCreateInfo& ci) {
    auto device = std::make_unique<GLDevice>(ci);
    if (!device->isInitialized())
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL device initialization failed"));
    return std::unique_ptr<RHIDevice>(std::move(device));
}

const AutoRegisterDeviceBackend _registerOpenGL(GraphicsBackend::OpenGL, &createOpenGLDevice);

}  // namespace
}  // namespace mulan::engine
