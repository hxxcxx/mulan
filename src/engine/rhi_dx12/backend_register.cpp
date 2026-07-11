/**
 * @file backend_register.cpp
 * @brief D3D12 后端自注册 —— 编译期向 DeviceFactory 注册创建函数。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "dx12_device.h"

namespace mulan::engine {

namespace {

std::unique_ptr<RHIDevice> createD3D12Device(const DeviceCreateInfo& ci) {
    return std::make_unique<DX12Device>(ci);
}

const AutoRegisterDeviceBackend _registerD3D12(GraphicsBackend::D3D12, &createD3D12Device);

}  // namespace

}  // namespace mulan::engine
