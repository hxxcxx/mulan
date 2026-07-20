/**
 * @file vk_platform_win32.cpp
 * @brief Vulkan Win32 Loader 与窗口 Surface 实现
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "vk_platform.h"

#include <mulan/core/profiling/profile.h>
#include <array>

namespace mulan::engine {

VulkanPlatformLoader::~VulkanPlatformLoader() {
    if (module_)
        FreeLibrary(static_cast<HMODULE>(module_));
}

PFN_vkGetInstanceProcAddr VulkanPlatformLoader::loadEntryPoint() noexcept {
    if (!module_)
        module_ = LoadLibraryW(L"vulkan-1.dll");
    if (!module_)
        return nullptr;
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(static_cast<HMODULE>(module_), "vkGetInstanceProcAddr"));
}

std::span<const char* const> vulkanPlatformSurfaceExtensions() noexcept {
    static constexpr std::array extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    return extensions;
}

vk::SurfaceKHR createVulkanPlatformSurface(vk::Instance instance, const NativeWindowHandle& window) {
    MULAN_PROFILE_ZONE();

    if (!instance || window.type != NativeWindowHandle::Type::Win32 || !window.valid())
        return nullptr;

    vk::Win32SurfaceCreateInfoKHR createInfo;
    createInfo.hinstance = reinterpret_cast<HINSTANCE>(window.win32.hInstance);
    createInfo.hwnd = reinterpret_cast<HWND>(window.win32.hWnd);
    return instance.createWin32SurfaceKHR(createInfo);
}

}  // namespace mulan::engine
