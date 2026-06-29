#include "Device.h"
#include "opengl/GLDevice.h"
#include "vulkan/VKDevice.h"
#include "d3d12/DX12Device.h"
#include "d3d11/DX11Device.h"

namespace mulan::engine {

std::shared_ptr<RHIDevice> RHIDevice::create(const DeviceCreateInfo& ci) {
    switch (ci.backend) {
    case GraphicsBackend::Vulkan:
        return std::make_shared<VKDevice>(ci);

    case GraphicsBackend::D3D12:
        return std::make_shared<DX12Device>(ci);

    case GraphicsBackend::D3D11:
        return std::make_shared<DX11Device>(ci);

    case GraphicsBackend::OpenGL: {
        auto dev = std::make_shared<GLDevice>(ci);
        if (!dev->isInitialized()) {
            std::fprintf(stderr, "[RHIDevice] GLDevice initialization failed\n");
            return nullptr;
        }
        return dev;
    }

    default:
        return nullptr;
    }
}

} // namespace mulan::engine
