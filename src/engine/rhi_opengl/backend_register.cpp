#include "backend.h"
#include "../rhi/device_factory.h"
#include "detail/gl_device.h"

namespace mulan::engine {
namespace {

std::unique_ptr<RHIDevice> createOpenGLDevice(const DeviceCreateInfo& ci) {
    return std::make_unique<GLDevice>(ci);
}

}  // namespace

const BackendModule& openGLBackendModule() {
    static const BackendModule module{ GraphicsBackend::OpenGL, "OpenGL", &createOpenGLDevice };
    return module;
}

}  // namespace mulan::engine
