/**
 * @file backend_register.cpp
 * @brief Vulkan 后端创建与模块入口
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "backend.h"
#include "detail/vk_device.h"

namespace mulan::engine {

namespace {

core::Result<std::unique_ptr<RHIDevice>> createVulkanDevice(const DeviceCreateInfo& ci) {
    return std::unique_ptr<RHIDevice>(std::make_unique<VKDevice>(ci));
}

}  // namespace

const BackendModule& vulkanBackendModule() {
    static const BackendModule module{ GraphicsBackend::Vulkan, "Vulkan", &createVulkanDevice };
    return module;
}

}  // namespace mulan::engine
