#include "backend.h"
#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "detail/dx11_device.h"

#include <exception>

namespace mulan::engine {
namespace {

core::Result<std::unique_ptr<RHIDevice>> createD3D11Device(const DeviceCreateInfo& ci) {
    try {
        auto device = std::make_unique<DX11Device>(ci);
        if (!device->isInitialized())
            return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device initialization failed"));
        return std::unique_ptr<RHIDevice>(std::move(device));
    } catch (const std::exception& error) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, error.what()));
    } catch (...) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device initialization failed"));
    }
}

}  // namespace

const BackendModule& d3d11BackendModule() {
    static const BackendModule module{ GraphicsBackend::D3D11, "D3D11", &createD3D11Device };
    return module;
}

}  // namespace mulan::engine
