#include "../rhi/device_factory.h"
#include "detail/gl_device.h"

#include <stdexcept>

namespace mulan::engine {
namespace {

std::unique_ptr<RHIDevice> createOpenGLDevice(const DeviceCreateInfo& ci) {
    auto device = std::make_unique<GLDevice>(ci);
    if (!device->isInitialized())
        throw std::runtime_error("OpenGL device initialization failed");
    return device;
}

const AutoRegisterDeviceBackend _registerOpenGL(GraphicsBackend::OpenGL, &createOpenGLDevice);

}  // namespace
}  // namespace mulan::engine
