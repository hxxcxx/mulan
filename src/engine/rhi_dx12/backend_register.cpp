/**
 * @file backend_register.cpp
 * @brief D3D12 后端自注册 —— 编译期向 DeviceFactory 注册创建函数。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "detail/dx12_device.h"

namespace mulan::engine {

namespace {

core::Result<std::unique_ptr<RHIDevice>> createD3D12Device(const DeviceCreateInfo& ci) {
    auto device = std::make_unique<DX12Device>(ci);
    if (!device->isInitialized())
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX12 device initialization failed"));
    return std::unique_ptr<RHIDevice>(std::move(device));
}

const AutoRegisterDeviceBackend _registerD3D12(GraphicsBackend::D3D12, &createD3D12Device);

}  // namespace

}  // namespace mulan::engine
