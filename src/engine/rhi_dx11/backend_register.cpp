#include "backend.h"
#include "../rhi/device_factory.h"
#include "detail/dx11_device.h"

namespace mulan::engine {
namespace {

std::unique_ptr<RHIDevice> createD3D11Device(const DeviceCreateInfo& ci) {
    return std::make_unique<DX11Device>(ci);
}

}  // namespace

const BackendModule& d3d11BackendModule() {
    static const BackendModule module{ GraphicsBackend::D3D11, "D3D11", &createD3D11Device };
    return module;
}

}  // namespace mulan::engine
