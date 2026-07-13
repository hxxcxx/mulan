#include "detail/vk_device.h"

// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE 和 VMA_IMPLEMENTATION
// 由 VKDevice.cpp 提供，此文件不再重复定义。

#include <set>
#include <cstdlib>

namespace mulan::engine {

// ============================================================
// Vulkan 验证层调试回调
// ============================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                      void* /*userData*/) {
    const char* message = data && data->pMessage ? data->pMessage : "<null validation message>";
    const int32_t messageId = data ? data->messageIdNumber : 0;
    const char* messageName = data && data->pMessageIdName ? data->pMessageIdName : "unknown";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("[Vulkan Validation] type=0x{:X}, id={} ({}): {}", type, messageId, messageName, message);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN("[Vulkan Validation] type=0x{:X}, id={} ({}): {}", type, messageId, messageName, message);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        LOG_INFO("[Vulkan Validation] type=0x{:X}, id={} ({}): {}", type, messageId, messageName, message);
    else
        LOG_DEBUG("[Vulkan Validation] type=0x{:X}, id={} ({}): {}", type, messageId, messageName, message);
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
    // 队列族
    auto queueFamilies = physical_device_.getQueueFamilyProperties();
    bool hasComputeQueue = false;
    bool hasGraphicsQueue = false;
    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            // 保留一个稳定的图形队列用于渲染；呈现队列在原生窗口实际创建后，
            // 再按 surface 分别选择。
            if (!hasGraphicsQueue) {
                graphics_queue_family_ = i;
                hasGraphicsQueue = true;
            }
        }
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) {
            hasComputeQueue = true;
        }
    }
    caps_.computeShader = hasComputeQueue;

    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCIs;
    // surface 不再参与 Device 创建。每个可用队列族均请求一个队列，
    // 以便稍后创建的 surface 在图形队列不能呈现时使用独立的呈现队列族。
    for (uint32_t qf = 0; qf < queueFamilies.size(); ++qf) {
        if (queueFamilies[qf].queueCount == 0)
            continue;
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

    // Query optional/core-promoted features before building the enable chain.
    vk::PhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreSupport;
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingSupport;
    dynamicRenderingSupport.pNext = &timelineSemaphoreSupport;
    vk::PhysicalDeviceFeatures2 featureSupport;
    featureSupport.pNext = &dynamicRenderingSupport;
    physical_device_.getFeatures2(&featureSupport);

    if (!dynamicRenderingSupport.dynamicRendering) {
        LOG_ERROR("[Vulkan] Required device feature is unavailable: dynamicRendering");
    }
    if (!timelineSemaphoreSupport.timelineSemaphore) {
        LOG_ERROR("[Vulkan] Required device feature is unavailable: timelineSemaphore");
    }

    vk::PhysicalDeviceFeatures features;
    features.fillModeNonSolid = featureSupport.features.fillModeNonSolid;
    features.depthClamp = featureSupport.features.depthClamp;

    // Both features are required by this backend: render passes use dynamic rendering,
    // while RHI fences are implemented with timeline semaphores.
    vk::PhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeature;
    timelineSemaphoreFeature.timelineSemaphore = true;
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature;
    dynamicRenderingFeature.dynamicRendering = true;
    dynamicRenderingFeature.pNext = &timelineSemaphoreFeature;

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
        LOG_ERROR("[Vulkan] Failed to load the Vulkan loader (vulkan-1.dll)");
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

    // Device 有意不绑定 surface：窗口和截图目标可在 Device 创建后再附加。
    // 因此预先启用平台 surface 扩展，而不是从第一个窗口推导扩展集合。
    instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef VK_KHR_win32_surface
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_KHR_xcb_surface
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

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
            const VkResult result = createFn(VkInstance(instance_), &dbgCI, nullptr, &messenger);
            if (result == VK_SUCCESS)
                debug_messenger_ = vk::DebugUtilsMessengerEXT(messenger);
            else
                LOG_WARN("[Vulkan] Validation messenger creation failed: VkResult={}", static_cast<int>(result));
        } else {
            LOG_WARN("[Vulkan] Validation requested but vkCreateDebugUtilsMessengerEXT is unavailable");
        }
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

    const VkResult allocatorResult = vmaCreateAllocator(&allocCI, &allocator_);
    if (allocatorResult != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] VMA allocator creation failed: VkResult={}", static_cast<int>(allocatorResult));
        return;
    }

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
    caps_.maxUniformBufferBindingSize = static_cast<uint32_t>(props.limits.maxUniformBufferRange);
    caps_.depthClamp = features.depthClamp;
    caps_.geometryShader = features.geometryShader;
    caps_.tessellationShader = features.tessellationShader;
    // caps_.computeShader 由上方 queue families 检查 (hasComputeQueue) 设置，这里不覆盖

    // --- 私有组件 ---
    upload_context_ = std::make_unique<VKUploadContext>(device_, allocator_, graphics_queue_family_, graphics_queue_);
    frame_scheduler_ = std::make_unique<VKFrameScheduler>(device_, graphics_queue_, graphics_queue_family_, allocator_,
                                                          caps_.minUniformBufferOffsetAlignment,
                                                          caps_.maxUniformBufferBindingSize);
    frame_scheduler_->initFrameContexts(ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2);
    resource_factory_ = std::make_unique<VKResourceFactory>(*this, device_, allocator_, *upload_context_);
    auto submissionFenceResult = createFence(0);
    if (!submissionFenceResult) {
        LOG_ERROR("[Vulkan] Submission timeline creation failed: {}", submissionFenceResult.error().message);
        return;
    }
    initializeSubmissionTracking(std::move(*submissionFenceResult));
    LOG_INFO("[Vulkan] Device initialized: gpu={}, api={}.{}.{}, maxMSAA={}, validation={}", props.deviceName.data(),
             VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
             VK_API_VERSION_PATCH(props.apiVersion), caps_.maxSampleCount, ci.enableValidation);
}

// ============================================================
// 关机清理
// ============================================================

void VKDevice::shutdown() {
    if (device_) {
        try {
            device_.waitIdle();
        } catch (const vk::Error& error) {
            LOG_ERROR("[Vulkan] Device wait-idle failed during shutdown: {}", error.what());
        }
    }

    // Destroy all objects that reference the device or allocator before their owners.
    drainDeferredReleases();
    shutdownSubmissionTracking();
    resource_factory_.reset();
    frame_scheduler_.reset();
    upload_context_.reset();

    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    if (device_) {
        device_.destroy();
        device_ = nullptr;
        graphics_queue_ = nullptr;
    }

    if (debug_messenger_) {
        auto destroyFn = VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT;
        if (destroyFn)
            destroyFn(VkInstance(instance_), VkDebugUtilsMessengerEXT(debug_messenger_), nullptr);
        debug_messenger_ = nullptr;
    }

    if (instance_) {
        instance_.destroy();
        instance_ = nullptr;
        physical_device_ = nullptr;
        LOG_DEBUG("[Vulkan] Device runtime shut down");
    }
}

}  // namespace mulan::engine
