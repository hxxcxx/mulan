/**
 * @file backend_register.cpp
 * @brief Vulkan 后端自注册 —— 编译期向 DeviceFactory 注册创建函数。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "vk_device.h"

namespace mulan::engine {

namespace {

std::unique_ptr<RHIDevice> createVulkanDevice(const DeviceCreateInfo& ci) {
    return std::make_unique<VKDevice>(ci);
}

const AutoRegisterDeviceBackend _registerVulkan(GraphicsBackend::Vulkan, &createVulkanDevice);

}  // namespace

}  // namespace mulan::engine
