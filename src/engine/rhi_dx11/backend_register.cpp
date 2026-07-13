#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "detail/dx11_device.h"

namespace mulan::engine {
namespace {

core::Result<std::unique_ptr<RHIDevice>> createD3D11Device(const DeviceCreateInfo& ci) {
    auto device = std::make_unique<DX11Device>(ci);
    if (!device->isInitialized())
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device initialization failed"));
    return std::unique_ptr<RHIDevice>(std::move(device));
}

const AutoRegisterDeviceBackend _registerD3D11(GraphicsBackend::D3D11, &createD3D11Device);

}  // namespace
}  // namespace mulan::engine
