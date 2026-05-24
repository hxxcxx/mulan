#include "Device.h"
#include "OpenGL/GLDevice.h"

#ifndef __EMSCRIPTEN__
#include "Vulkan/VKDevice.h"
#include "D3D12/DX12Device.h"
#include "D3D11/DX11Device.h"
#endif

namespace mulan::engine {

std::shared_ptr<RHIDevice> RHIDevice::create(const DeviceCreateInfo& ci) {
    switch (ci.backend) {
#ifndef __EMSCRIPTEN__
    case GraphicsBackend::Vulkan:
        return std::make_shared<VKDevice>(ci);

    case GraphicsBackend::D3D12:
        return std::make_shared<DX12Device>(ci);

    case GraphicsBackend::D3D11:
        return std::make_shared<DX11Device>(ci);
#endif

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

} // namespace mulan::Engine
