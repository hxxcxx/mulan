#include "device.h"
#include "vulkan/vk_device.h"
#include "d3d12/dx12_device.h"
#include "../engine_error_code.h"

#include <mulan/core/result/error.h>

namespace mulan::engine {

std::expected<std::shared_ptr<RHIDevice>, core::Error> RHIDevice::create(const DeviceCreateInfo& ci) {
    try {
        switch (ci.backend) {
        case GraphicsBackend::Vulkan:
            return std::make_shared<VKDevice>(ci);

        case GraphicsBackend::D3D12:
            return std::make_shared<DX12Device>(ci);

        default:
            return std::unexpected(
                core::Error::make(core::ErrorCode::NotSupported,
                                  "Graphics backend not implemented"));
        }
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, e.what()));
    }
}

} // namespace mulan::engine
