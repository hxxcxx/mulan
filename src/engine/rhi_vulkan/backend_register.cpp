/**
 * @file backend_register.cpp
 * @brief Vulkan 后端创建与模块入口
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "../rhi/device_factory.h"
#include "../rhi/engine_error_code.h"
#include "backend.h"
#include "detail/vk_device.h"

#include <exception>

namespace mulan::engine {

namespace {

core::Result<std::unique_ptr<RHIDevice>> createVulkanDevice(const DeviceCreateInfo& ci) {
    try {
        return std::unique_ptr<RHIDevice>(std::make_unique<VKDevice>(ci));
    } catch (const std::exception& error) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, error.what()));
    } catch (...) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Vulkan device initialization failed"));
    }
}

}  // namespace

const BackendModule& vulkanBackendModule() {
    static const BackendModule module{ GraphicsBackend::Vulkan, "Vulkan", &createVulkanDevice };
    return module;
}

}  // namespace mulan::engine
