/**
 * @file backend_register.cpp
 * @brief D3D12 后端创建与模块入口
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "backend.h"
#include "detail/dx12_device.h"

namespace mulan::engine {

namespace {

core::Result<std::unique_ptr<RHIDevice>> createD3D12Device(const DeviceCreateInfo& ci) {
    auto device = std::make_unique<DX12Device>(ci);
    if (!device->isInitialized())
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX12 device initialization failed"));
    return std::unique_ptr<RHIDevice>(std::move(device));
}

}  // namespace

const BackendModule& d3d12BackendModule() {
    static const BackendModule module{ GraphicsBackend::D3D12, "D3D12", &createD3D12Device };
    return module;
}

}  // namespace mulan::engine
