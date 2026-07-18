/**
 * @file vk_platform.h
 * @brief Vulkan Loader 与窗口 Surface 的平台边界
 * @author hxxcxx
 * @date 2026-07-18
 */

#pragma once

#include "../detail/vk_common.h"
#include "../../rhi/window.h"

#include <span>

namespace mulan::engine {

/**
 * @brief 持有当前平台 Vulkan Loader 的进程模块引用
 *
 * Loader 必须覆盖 Vulkan 实例和设备的完整生命周期，避免动态分发器保存悬空函数地址。
 */
class VulkanPlatformLoader final {
public:
    VulkanPlatformLoader() = default;
    ~VulkanPlatformLoader();

    VulkanPlatformLoader(const VulkanPlatformLoader&) = delete;
    VulkanPlatformLoader& operator=(const VulkanPlatformLoader&) = delete;

    PFN_vkGetInstanceProcAddr loadEntryPoint() noexcept;

private:
    void* module_ = nullptr;
};

/// 创建 Vulkan 实例时必须启用的当前窗口平台 Surface 扩展。
std::span<const char* const> vulkanPlatformSurfaceExtensions() noexcept;

/// 从统一原生窗口句柄创建当前平台的 Vulkan Surface。
vk::SurfaceKHR createVulkanPlatformSurface(vk::Instance instance, const NativeWindowHandle& window);

}  // namespace mulan::engine
