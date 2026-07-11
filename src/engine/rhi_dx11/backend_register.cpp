#include "../rhi/device_factory.h"
#include "detail/dx11_device.h"

namespace mulan::engine {
namespace {

std::unique_ptr<RHIDevice> createD3D11Device(const DeviceCreateInfo& ci) {
    return std::make_unique<DX11Device>(ci);
}

const AutoRegisterDeviceBackend _registerD3D11(GraphicsBackend::D3D11, &createD3D11Device);

}  // namespace
}  // namespace mulan::engine
