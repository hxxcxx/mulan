#include "../rhi/device_factory.h"
#include "detail/gl_device.h"

namespace mulan::engine {
namespace {

std::unique_ptr<RHIDevice> createOpenGLDevice(const DeviceCreateInfo& ci) {
    return std::make_unique<GLDevice>(ci);
}

const AutoRegisterDeviceBackend _registerOpenGL(GraphicsBackend::OpenGL, &createOpenGLDevice);

}  // namespace
}  // namespace mulan::engine
