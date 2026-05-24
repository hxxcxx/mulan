#include "VKDevice.h"

// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE 和 VMA_IMPLEMENTATION
// 由 VKDevice.cpp 提供，此文件不再重复定义。

#include <set>
#include <cstdio>
#include <cstdlib>

namespace mulan::engine {

// ============================================================
// Vulkan 验证层调试回调
// ============================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*userData*/)
{
    const char* prefix = "[VK]";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        prefix = "[VK ERROR]";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        prefix = "[VK WARN]";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        prefix = "[VK INFO]";

    fprintf(stderr, "%s %s\n", prefix, data->pMessage);
    fflush(stderr);
    return VK_FALSE;
}

// ============================================================
// 构造 / 析构
// ============================================================

VKDevice::VKDevice(const CreateInfo& ci) {
    init(ci);
}

VKDevice::VKDevice(const DeviceCreateInfo& ci) {
    CreateInfo vkCI;
    vkCI.enableValidation = ci.enableValidation;
    vkCI.window           = ci.window;
    vkCI.renderConfig     = ci.renderConfig;
    vkCI.appName          = ci.appName;
    init(vkCI);
}

VKDevice::~VKDevice() {
    m_device.waitIdle();
    clearFramebufferCache();
    m_frameCmdList.reset();
    m_frameContexts.clear();
    m_uploadContext.reset();
    m_descriptorAllocators.clear();
    m_standaloneAllocators.clear();
    while (!m_swapChains.empty()) {
        destroy(m_swapChains.back());
    }

    // 销毁 per-image renderFinished 信号量
    for (auto& sem : m_renderFinishedSemaphores) {
        if (m_device && sem) m_device.destroySemaphore(sem);
    }
    m_renderFinishedSemaphores.clear();

    // 销毁 RenderPass Cache
    for (auto& [key, rp] : m_renderPassCache) {
        if (m_device && rp) m_device.destroyRenderPass(rp);
    }
    m_renderPassCache.clear();

    shutdown();
}

// ============================================================
// Device 信息
// ============================================================

GraphicsBackend VKDevice::backend() const {
    return GraphicsBackend::Vulkan;
}

const GPUDeviceCapabilities& VKDevice::capabilities() const {
    return m_caps;
}

const RenderConfig& VKDevice::renderConfig() const {
    return m_renderConfig;
}

Mat4 VKDevice::clipSpaceCorrectionMatrix() const {
    // Vulkan NDC: Y 朝下, z∈[0,1]
    // 标准右手(OpenGL): Y 朝上, z∈[-1,1]
    // 修正: 翻转 Y, 将 z 从 [-1,1] 映射到 [0,1]
    Mat4 c(1.0);
    c[1][1] = -1.0;   // Y 翻转
    c[2][2] =  0.5;   // z scale
    c[3][2] =  0.5;   // z offset
    return c;
}

// ============================================================
// 物理设备选择
// ============================================================

void VKDevice::pickPhysicalDevice(const std::vector<vk::PhysicalDevice>& devices) {
    for (auto& device : devices) {
        auto properties = device.getProperties();
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            m_physicalDevice = device;
            return;
        }
    }
    if (!devices.empty()) {
        m_physicalDevice = devices[0];
    }
}

// ============================================================
// 逻辑设备创建
// ============================================================

void VKDevice::createLogicalDevice(bool enableValidation) {
    // Queue families
    auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            m_graphicsQueueFamily = i;
        }
        if (m_surface) {
            auto supported = m_physicalDevice.getSurfaceSupportKHR(i, m_surface);
            if (supported) {
                m_presentQueueFamily = i;
            }
        }
    }

    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCIs;
    std::set<uint32_t> uniqueQueues = { m_graphicsQueueFamily };
    if (m_surface) uniqueQueues.insert(m_presentQueueFamily);

    for (uint32_t qf : uniqueQueues) {
        vk::DeviceQueueCreateInfo qCI;
        qCI.queueFamilyIndex = qf;
        qCI.queueCount       = 1;
        qCI.pQueuePriorities = &queuePriority;
        queueCIs.push_back(qCI);
    }

    // Device extensions — 仅在有 surface 时需要 swapchain
    std::vector<const char*> deviceExtensions;
    if (m_surface) {
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    // Features
    vk::PhysicalDeviceFeatures features;
    features.fillModeNonSolid = true;  // wireframe
    features.depthClamp       = true;

    vk::DeviceCreateInfo deviceCI;
    deviceCI.queueCreateInfoCount    = static_cast<uint32_t>(queueCIs.size());
    deviceCI.pQueueCreateInfos       = queueCIs.data();
    deviceCI.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    deviceCI.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCI.pEnabledFeatures        = &features;

    m_device = m_physicalDevice.createDevice(deviceCI);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

    m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamily, 0);
    if (m_presentQueueFamily != m_graphicsQueueFamily || m_surface) {
        m_presentQueue = m_device.getQueue(m_presentQueueFamily, 0);
    } else {
        m_presentQueue = m_graphicsQueue;
    }
}

// ============================================================
// Surface 创建
// ============================================================

vk::SurfaceKHR VKDevice::createSurface(const NativeWindowHandle& window) {
    switch (window.type) {
#ifdef _WIN32
    case NativeWindowHandle::Type::Win32: {
        vk::Win32SurfaceCreateInfoKHR ci;
        ci.hinstance = reinterpret_cast<HINSTANCE>(window.win32.hInstance);
        ci.hwnd      = reinterpret_cast<HWND>(window.win32.hWnd);
        return m_instance.createWin32SurfaceKHR(ci);
    }
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    case NativeWindowHandle::Type::XCB: {
        vk::XcbSurfaceCreateInfoKHR ci;
        ci.connection = reinterpret_cast<xcb_connection_t*>(window.xcb.connection);
        ci.window     = static_cast<xcb_window_t>(window.xcb.window);
        return m_instance.createXcbSurfaceKHR(ci);
    }
#endif

    default:
        return nullptr;
    }
}

// ============================================================
// 完整初始化流程
// ============================================================

void VKDevice::init(const CreateInfo& ci) {
    m_nativeWindow = ci.window;
    m_renderConfig = ci.renderConfig;
    m_frameCount   = ci.frameCount > 0 ? ci.frameCount : ci.renderConfig.bufferCount;

    // --- Dynamic dispatch loader ---
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

#ifdef _WIN32
    HMODULE vulkanModule = GetModuleHandleW(L"vulkan-1.dll");
    if (!vulkanModule) vulkanModule = LoadLibraryW(L"vulkan-1.dll");
    if (vulkanModule) {
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));
    }
#elif defined(__linux__)
    // Linux: 延迟到 SDL/GLFW/vulkan-1.so dlopen
    // TODO: dlopen("libvulkan.so.1") 加载
#endif

    if (!vkGetInstanceProcAddr) {
        std::fprintf(stderr, "[VKDevice] Failed to load Vulkan loader (vulkan-1.dll)\n");
        std::abort();
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    // --- Instance ---
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName   = ci.appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "MulanGeo";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> instanceExtensions;

    // 仅在需要窗口时添加 Surface 扩展
    if (ci.window.valid()) {
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }

    // 根据窗口类型添加平台 Surface 扩展
    switch (ci.window.type) {
#ifdef VK_KHR_win32_surface
        case NativeWindowHandle::Type::Win32:
            instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
            break;
#endif
#ifdef VK_KHR_xcb_surface
        case NativeWindowHandle::Type::XCB:
            instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
            break;
#endif
        default:
            break;
    }

    std::vector<const char*> instanceLayers;
    if (ci.enableValidation) {
        instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo instanceCI;
    instanceCI.pApplicationInfo        = &appInfo;
    instanceCI.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
    instanceCI.ppEnabledExtensionNames = instanceExtensions.data();
    instanceCI.enabledLayerCount       = static_cast<uint32_t>(instanceLayers.size());
    instanceCI.ppEnabledLayerNames     = instanceLayers.data();

    m_instance = vk::createInstance(instanceCI);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

    // --- 调试回调（使用 C API 避免 vulkan-hpp 回调类型不匹配）---
    if (ci.enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity  = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        dbgCI.messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback  = vkDebugCallback;

        auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(VkInstance(m_instance), "vkCreateDebugUtilsMessengerEXT"));
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        if (createFn) {
            createFn(VkInstance(m_instance), &dbgCI, nullptr, &messenger);
            m_debugMessenger = vk::DebugUtilsMessengerEXT(messenger);
        }
    }

    // --- Surface（根据平台创建）---
    if (ci.window.valid()) {
        m_surface = createSurface(ci.window);
    }

    // --- Physical Device ---
    auto devices = m_instance.enumeratePhysicalDevices();
    pickPhysicalDevice(devices);

    // --- Device ---
    createLogicalDevice(ci.enableValidation);

    // --- VMA ---
    VmaVulkanFunctions vkFuncs{};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    auto vkGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vkGetInstanceProcAddr(VkInstance(m_instance), "vkGetDeviceProcAddr"));
    vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddrFn;

    VmaAllocatorCreateInfo allocCI{};
    allocCI.physicalDevice   = VkPhysicalDevice(m_physicalDevice);
    allocCI.device           = VkDevice(m_device);
    allocCI.instance         = VkInstance(m_instance);
    allocCI.vulkanApiVersion = VK_API_VERSION_1_3;
    allocCI.pVulkanFunctions = &vkFuncs;

    vmaCreateAllocator(&allocCI, &m_allocator);

    // --- Capabilities ---
    m_caps.backend           = GraphicsBackend::Vulkan;
    auto props               = m_physicalDevice.getProperties();
    m_caps.maxTextureSize    = props.limits.maxImageDimension2D;
    m_caps.maxTextureAniso   = static_cast<uint32_t>(props.limits.maxSamplerAnisotropy);
    m_caps.minUniformBufferOffsetAlignment = props.limits.minUniformBufferOffsetAlignment;
    m_caps.depthClamp        = true;
    m_caps.geometryShader    = true;
    m_caps.computeShader     = true;

    // --- 私有组件 ---
    m_uploadContext = std::make_unique<VKUploadContext>(
        m_device, m_allocator, m_graphicsQueueFamily, m_graphicsQueue);

    // per-frame descriptor allocators (will be properly sized in initFrameContexts)
    m_descriptorAllocators.clear();
    m_descriptorAllocators.push_back(std::make_unique<VKDescriptorAllocator>(m_device));
    m_descriptorAllocators.push_back(std::make_unique<VKDescriptorAllocator>(m_device));

    // FrameContext 在 createSwapChain 时初始化（需要知道 swapchain image count）
}

// ============================================================
// 关机清理
// ============================================================

void VKDevice::shutdown() {
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }

    if (m_surface) {
        m_instance.destroySurfaceKHR(m_surface);
        m_surface = nullptr;
    }

    if (m_device) {
        m_device.destroy();
        m_device = nullptr;
    }

    if (m_debugMessenger) {
        auto destroyFn = VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT;
        if (destroyFn)
            destroyFn(VkInstance(m_instance), VkDebugUtilsMessengerEXT(m_debugMessenger), nullptr);
        m_debugMessenger = nullptr;
    }

    if (m_instance) {
        m_instance.destroy();
        m_instance = nullptr;
    }
}

} // namespace mulan::Engine
