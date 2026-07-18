/**
 * @file vk_platform_x11.cpp
 * @brief Vulkan Linux Loader 与 X11/XCB Surface 实现
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "vk_platform.h"

#include <array>
#include <dlfcn.h>
#include <xcb/xcb.h>

namespace mulan::engine {

VulkanPlatformLoader::~VulkanPlatformLoader() {
    if (module_)
        dlclose(module_);
}

PFN_vkGetInstanceProcAddr VulkanPlatformLoader::loadEntryPoint() noexcept {
    if (!module_) {
        module_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!module_)
            module_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!module_)
        return nullptr;
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module_, "vkGetInstanceProcAddr"));
}

std::span<const char* const> vulkanPlatformSurfaceExtensions() noexcept {
    static constexpr std::array extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    };
    return extensions;
}

vk::SurfaceKHR createVulkanPlatformSurface(vk::Instance instance, const NativeWindowHandle& window) {
    if (!instance || window.type != NativeWindowHandle::Type::X11 || !window.valid())
        return nullptr;

    vk::XcbSurfaceCreateInfoKHR createInfo;
    createInfo.connection = reinterpret_cast<xcb_connection_t*>(window.x11.connection);
    createInfo.window = static_cast<xcb_window_t>(window.x11.window);
    return instance.createXcbSurfaceKHR(createInfo);
}

}  // namespace mulan::engine
