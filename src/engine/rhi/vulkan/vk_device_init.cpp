#include "vk_device.h"

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

VKDevice::VKDevice(const DeviceCreateInfo& ci) {
    init(ci);
}

VKDevice::~VKDevice() {
    device_.waitIdle();

    // 诊断：打印 ~VKDevice 入口处仍存活的 VMA allocation
    if (allocator_) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator_, &stats);
    }

    clearFramebufferCache();
    frame_cmd_list_.reset();
    frame_contexts_.clear();
    upload_context_.reset();
    descriptor_allocators_.clear();
    standalone_allocators_.clear();

    // 销毁 per-image renderFinished 信号量
    for (auto& sem : render_finished_semaphores_) {
        if (device_ && sem) device_.destroySemaphore(sem);
    }
    render_finished_semaphores_.clear();

    // 销毁 RenderPass Cache
    for (auto& [key, rp] : render_pass_cache_) {
        if (device_ && rp) device_.destroyRenderPass(rp);
    }
    render_pass_cache_.clear();

    shutdown();
}

// ============================================================
// Device 信息
// ============================================================

GraphicsBackend VKDevice::backend() const {
    return GraphicsBackend::Vulkan;
}

const GPUDeviceCapabilities& VKDevice::capabilities() const {
    return caps_;
}

const RenderConfig& VKDevice::renderConfig() const {
    return render_config_;
}

math::Mat4 VKDevice::clipSpaceCorrectionMatrix() const {
    // Vulkan NDC: Y 朝下, z∈[0,1]
    // 标准右手(OpenGL): Y 朝上, z∈[-1,1]
    // 修正: 翻转 Y, 将 z 从 [-1,1] 映射到 [0,1]
    math::Mat4 c(1.0);
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
            physical_device_ = device;
            return;
        }
    }
    if (!devices.empty()) {
        physical_device_ = devices[0];
    }
}

// ============================================================
// 逻辑设备创建
// ============================================================

void VKDevice::createLogicalDevice(bool enableValidation) {
    // Queue families
    auto queueFamilies = physical_device_.getQueueFamilyProperties();
    bool hasComputeQueue = false;
    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_queue_family_ = i;
        }
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) {
            hasComputeQueue = true;
        }
        if (surface_) {
            auto supported = physical_device_.getSurfaceSupportKHR(i, surface_);
            if (supported) {
                present_queue_family_ = i;
            }
        }
    }
    caps_.computeShader = hasComputeQueue;

    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCIs;
    std::set<uint32_t> uniqueQueues = { graphics_queue_family_ };
    if (surface_) uniqueQueues.insert(present_queue_family_);

    for (uint32_t qf : uniqueQueues) {
        vk::DeviceQueueCreateInfo qCI;
        qCI.queueFamilyIndex = qf;
        qCI.queueCount       = 1;
        qCI.pQueuePriorities = &queuePriority;
        queueCIs.push_back(qCI);
    }

    // Device extensions — 仅在有 surface 时需要 swapchain
    std::vector<const char*> deviceExtensions;
    if (surface_) {
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

    device_ = physical_device_.createDevice(deviceCI);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device_);

    graphics_queue_ = device_.getQueue(graphics_queue_family_, 0);
    if (present_queue_family_ != graphics_queue_family_ || surface_) {
        present_queue_ = device_.getQueue(present_queue_family_, 0);
    } else {
        present_queue_ = graphics_queue_;
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
        return instance_.createWin32SurfaceKHR(ci);
    }
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    case NativeWindowHandle::Type::XCB: {
        vk::XcbSurfaceCreateInfoKHR ci;
        ci.connection = reinterpret_cast<xcb_connection_t*>(window.xcb.connection);
        ci.window     = static_cast<xcb_window_t>(window.xcb.window);
        return instance_.createXcbSurfaceKHR(ci);
    }
#endif

    default:
        return nullptr;
    }
}

// ============================================================
// 完整初始化流程
// ============================================================

void VKDevice::init(const DeviceCreateInfo& ci) {
    native_window_ = ci.window;
    render_config_ = ci.renderConfig;
    frame_count_   = ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2;

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
    appInfo.pEngineName        = "mulan";
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

    instance_ = vk::createInstance(instanceCI);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_);

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
            vkGetInstanceProcAddr(VkInstance(instance_), "vkCreateDebugUtilsMessengerEXT"));
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        if (createFn) {
            createFn(VkInstance(instance_), &dbgCI, nullptr, &messenger);
            debug_messenger_ = vk::DebugUtilsMessengerEXT(messenger);
        }
    }

    // --- Surface（根据平台创建）---
    if (ci.window.valid()) {
        surface_ = createSurface(ci.window);
    }

    // --- Physical Device ---
    auto devices = instance_.enumeratePhysicalDevices();
    pickPhysicalDevice(devices);

    // --- Device ---
    createLogicalDevice(ci.enableValidation);

    // --- VMA ---
    VmaVulkanFunctions vkFuncs{};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    auto vkGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vkGetInstanceProcAddr(VkInstance(instance_), "vkGetDeviceProcAddr"));
    vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddrFn;

    VmaAllocatorCreateInfo allocCI{};
    allocCI.physicalDevice   = VkPhysicalDevice(physical_device_);
    allocCI.device           = VkDevice(device_);
    allocCI.instance         = VkInstance(instance_);
    allocCI.vulkanApiVersion = VK_API_VERSION_1_3;
    allocCI.pVulkanFunctions = &vkFuncs;

    vmaCreateAllocator(&allocCI, &allocator_);

    // --- Capabilities ---
    caps_.backend           = GraphicsBackend::Vulkan;
    auto props               = physical_device_.getProperties();
    auto features             = physical_device_.getFeatures();
    caps_.maxTextureSize    = props.limits.maxImageDimension2D;
    caps_.maxTextureAniso   = static_cast<uint32_t>(props.limits.maxSamplerAnisotropy);
    caps_.minUniformBufferOffsetAlignment = props.limits.minUniformBufferOffsetAlignment;
    caps_.depthClamp        = features.depthClamp;
    caps_.geometryShader    = features.geometryShader;
    caps_.tessellationShader = features.tessellationShader;
    // caps_.computeShader 由上方 queue families 检查 (hasComputeQueue) 设置，这里不覆盖

    // --- 私有组件 ---
    upload_context_ = std::make_unique<VKUploadContext>(
        device_, allocator_, graphics_queue_family_, graphics_queue_);

    // per-frame descriptor allocators (will be properly sized in initFrameContexts)
    descriptor_allocators_.clear();
    descriptor_allocators_.push_back(std::make_unique<VKDescriptorAllocator>(device_));
    descriptor_allocators_.push_back(std::make_unique<VKDescriptorAllocator>(device_));

    // FrameContext 在 createSwapChain 时初始化（需要知道 swapchain image count）
}

// ============================================================
// 关机清理
// ============================================================

void VKDevice::shutdown() {
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
    }

    if (surface_) {
        instance_.destroySurfaceKHR(surface_);
    }

    if (device_) {
        device_.destroy();
    }

    if (debug_messenger_) {
        auto destroyFn = VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT;
        if (destroyFn)
            destroyFn(VkInstance(instance_), VkDebugUtilsMessengerEXT(debug_messenger_), nullptr);
    }

    if (instance_) {
        instance_.destroy();
    }
}

} // namespace mulan::engine
