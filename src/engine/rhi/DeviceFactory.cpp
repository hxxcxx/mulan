#include "Device.h"
#include "vulkan/VKDevice.h"
#include "d3d12/DX12Device.h"

namespace mulan::engine {

std::shared_ptr<RHIDevice> RHIDevice::create(const DeviceCreateInfo& ci) {
    switch (ci.backend) {
    case GraphicsBackend::Vulkan:
        return std::make_shared<VKDevice>(ci);

    case GraphicsBackend::D3D12:
        return std::make_shared<DX12Device>(ci);

    default:
        return nullptr;
    }
}

} // namespace mulan::engine
