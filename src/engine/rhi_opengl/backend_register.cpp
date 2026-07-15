#include "backend.h"
#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "detail/gl_device.h"

#include <exception>

namespace mulan::engine {
namespace {

Result<std::unique_ptr<RHIDevice>> createOpenGLDevice(const DeviceCreateInfo& ci) {
    try {
        auto device = std::make_unique<GLDevice>(ci);
        if (!device->isInitialized())
            return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL device initialization failed"));
        return std::unique_ptr<RHIDevice>(std::move(device));
    } catch (const std::exception& error) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, error.what()));
    } catch (...) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL device initialization failed"));
    }
}

}  // namespace

const BackendModule& openGLBackendModule() {
    static const BackendModule module{ GraphicsBackend::OpenGL, "OpenGL", &createOpenGLDevice };
    return module;
}

}  // namespace mulan::engine
