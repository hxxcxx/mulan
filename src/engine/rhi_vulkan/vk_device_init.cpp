#include "detail/vk_device.h"

// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE 和 VMA_IMPLEMENTATION
// 由 VKDevice.cpp 提供，此文件不再重复定义。

#include <set>
#include <cstdio>
#include <cstdlib>

namespace mulan::engine {

// ============================================================
// Vulkan 验证层调试回调
// ============================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                      void* /*userData*/) {
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

static uint32_t sampleCountFromFlags(vk::SampleCountFlags flags, uint32_t requested) {
    const uint32_t candidates[] = { 8, 4, 2, 1 };
    for (uint32_t sample : candidates) {
        if (sample > requested)
            continue;
        if (sample == 8 && (flags & vk::SampleCountFlagBits::e8))
            return 8;
        if (sample == 4 && (flags & vk::SampleCountFlagBits::e4))
            return 4;
        if (sample == 2 && (flags & vk::SampleCountFlagBits::e2))
            return 2;
        if (sample == 1)
            return 1;
    }
    return 1;
}

static RenderConfig::MSAALevel toMsaaLevel(uint32_t samples) {
    switch (samples) {
    case 8: return RenderConfig::MSAALevel::x8;
    case 4: return RenderConfig::MSAALevel::x4;
    case 2: return RenderConfig::MSAALevel::x2;
    default: return RenderConfig::MSAALevel::None;
    }
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

    frame_scheduler_.reset();
    resource_factory_.reset();
    upload_context_.reset();
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
    c[1][1] = -1.0;  // Y 翻转
    c[2][2] = 0.5;   // z scale
    c[3][2] = 0.5;   // z offset
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
    if (surface_)
        uniqueQueues.insert(present_queue_family_);

    for (uint32_t qf : uniqueQueues) {
        vk::DeviceQueueCreateInfo qCI;
        qCI.queueFamilyIndex = qf;
        qCI.queueCount = 1;
        qCI.pQueuePriorities = &queuePriority;
        queueCIs.push_back(qCI);
    }

    // Device extensions — 视口和离屏缩略图可能在同一进程内先后创建 Vulkan 设备。
    // Vulkan-Hpp 使用全局动态分发器；离屏设备若不加载 swapchain 入口，会覆盖主视口后续
    // acquire/present 所需的函数指针。桌面 GPU 上始终启用该扩展以保持分发器完整。
    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    // Features
    vk::PhysicalDeviceFeatures features;
    features.fillModeNonSolid = true;  // wireframe
    features.depthClamp = true;

    // Dynamic rendering feature (VK_KHR_dynamic_rendering)
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature;
    dynamicRenderingFeature.dynamicRendering = true;

    vk::DeviceCreateInfo deviceCI;
    deviceCI.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
    deviceCI.pQueueCreateInfos = queueCIs.data();
    deviceCI.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCI.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCI.pEnabledFeatures = &features;
    deviceCI.pNext = &dynamicRenderingFeature;

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
        ci.hwnd = reinterpret_cast<HWND>(window.win32.hWnd);
        return instance_.createWin32SurfaceKHR(ci);
    }
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    case NativeWindowHandle::Type::XCB: {
        vk::XcbSurfaceCreateInfoKHR ci;
        ci.connection = reinterpret_cast<xcb_connection_t*>(window.xcb.connection);
        ci.window = static_cast<xcb_window_t>(window.xcb.window);
        return instance_.createXcbSurfaceKHR(ci);
    }
#endif

    default: return nullptr;
    }
}

// ============================================================
// 完整初始化流程
// ============================================================

void VKDevice::init(const DeviceCreateInfo& ci) {
    native_window_ = ci.window;
    render_config_ = ci.renderConfig;

    // --- Dynamic dispatch loader ---
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

#ifdef _WIN32
    HMODULE vulkanModule = GetModuleHandleW(L"vulkan-1.dll");
    if (!vulkanModule)
        vulkanModule = LoadLibraryW(L"vulkan-1.dll");
    if (vulkanModule) {
        vkGetInstanceProcAddr =
                reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));
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
    appInfo.pApplicationName = ci.appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "mulan";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> instanceExtensions;

    // 仅在需要窗口时添加 Surface 扩展
    if (ci.window.valid()) {
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }

    // 根据窗口类型添加平台 Surface 扩展
    switch (ci.window.type) {
#ifdef VK_KHR_win32_surface
    case NativeWindowHandle::Type::Win32: instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME); break;
#endif
#ifdef VK_KHR_xcb_surface
    case NativeWindowHandle::Type::XCB: instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME); break;
#endif
    default: break;
    }

    std::vector<const char*> instanceLayers;
    if (ci.enableValidation) {
        instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo instanceCI;
    instanceCI.pApplicationInfo = &appInfo;
    instanceCI.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceCI.ppEnabledExtensionNames = instanceExtensions.data();
    instanceCI.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
    instanceCI.ppEnabledLayerNames = instanceLayers.data();

    instance_ = vk::createInstance(instanceCI);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_);

    // --- 调试回调（使用 C API 避免 vulkan-hpp 回调类型不匹配）---
    if (ci.enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = vkDebugCallback;

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
    allocCI.physicalDevice = VkPhysicalDevice(physical_device_);
    allocCI.device = VkDevice(device_);
    allocCI.instance = VkInstance(instance_);
    allocCI.vulkanApiVersion = VK_API_VERSION_1_3;
    allocCI.pVulkanFunctions = &vkFuncs;

    vmaCreateAllocator(&allocCI, &allocator_);

    // --- Capabilities ---
    caps_.backend = GraphicsBackend::Vulkan;
    auto props = physical_device_.getProperties();
    auto features = physical_device_.getFeatures();
    caps_.maxTextureSize = props.limits.maxImageDimension2D;
    caps_.maxTextureAniso = static_cast<uint32_t>(props.limits.maxSamplerAnisotropy);
    const auto framebufferSampleCounts =
            props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    caps_.maxSampleCount = sampleCountFromFlags(framebufferSampleCounts, 8);
    render_config_.msaa = toMsaaLevel(sampleCountFromFlags(framebufferSampleCounts, render_config_.sampleCount()));
    caps_.minUniformBufferOffsetAlignment = props.limits.minUniformBufferOffsetAlignment;
    caps_.depthClamp = features.depthClamp;
    caps_.geometryShader = features.geometryShader;
    caps_.tessellationShader = features.tessellationShader;
    // caps_.computeShader 由上方 queue families 检查 (hasComputeQueue) 设置，这里不覆盖

    // --- 私有组件 ---
    upload_context_ = std::make_unique<VKUploadContext>(device_, allocator_, graphics_queue_family_, graphics_queue_);
    frame_scheduler_ = std::make_unique<VKFrameScheduler>(device_, graphics_queue_, graphics_queue_family_);
    frame_scheduler_->initFrameContexts(ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2);
    resource_factory_ = std::make_unique<VKResourceFactory>(*this, device_, allocator_, *upload_context_);
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

}  // namespace mulan::engine
