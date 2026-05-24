/**
 * @file VKDevice.h
 * @brief Vulkan设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "VkCommon.h"
#include "../Device.h"
#include "../../Window.h"
#include "VkConvert.h"
#include "VKBuffer.h"
#include "VKTexture.h"
#include "VKShader.h"
#include "VKPipelineState.h"
#include "VKCommandList.h"
#include "VKSwapChain.h"
#include "VKRenderTarget.h"
#include "VKSampler.h"
#include "VKFence.h"
#include "VKUploadContext.h"
#include "VKFrameContext.h"
#include "VKDescriptorAllocator.h"

#include <vector>
#include <memory>
#include <set>
#include <array>
#include <span>
#include <unordered_map>

namespace mulan::engine {

class VKDevice : public RHIDevice {
public:
    struct CreateInfo {
        bool               enableValidation = true;
        NativeWindowHandle window;
        RenderConfig       renderConfig;
        uint32_t           frameCount       = 0;
        const char*        appName          = "MulanGeo";
    };

    explicit VKDevice(const CreateInfo& ci = {});
    explicit VKDevice(const DeviceCreateInfo& ci);
    ~VKDevice();

    // --- Device 信息 ---
    GraphicsBackend backend() const override;
    const GPUDeviceCapabilities& capabilities() const override;
    const RenderConfig& renderConfig() const override;
    Mat4 clipSpaceCorrectionMatrix() const override;

    // --- 资源创建 ---
    ResourcePtr<Buffer>        createBuffer(const BufferDesc& desc) override;
    ResourcePtr<Texture>       createTexture(const TextureDesc& desc) override;
    ResourcePtr<Shader>        createShader(const ShaderDesc& desc) override;
    ResourcePtr<PipelineState> createPipelineState(const GraphicsPipelineDesc& desc) override;
    ResourcePtr<CommandList>   createCommandList() override;
    ResourcePtr<SwapChain>     createSwapChain(const SwapChainDesc& desc) override;
    ResourcePtr<RenderTarget>  createRenderTarget(const RenderTargetDesc& desc) override;
    ResourcePtr<Sampler>       createSampler(const SamplerDesc& desc) override;
    ResourcePtr<Fence>         createFence(uint64_t initialValue = 0) override;

    // --- 资源销毁 ---
    void destroy(Buffer* resource) override;
    void destroy(Texture* resource) override;
    void destroy(Shader* resource) override;
    void destroy(PipelineState* resource) override;
    void destroy(CommandList* resource) override;
    void destroy(SwapChain* resource) override;
    void destroy(RenderTarget* resource) override;
    void destroy(Sampler* resource) override;
    void destroy(Fence* resource) override;

    // --- 提交命令 ---
    void executeCommandLists(CommandList** cmdLists, uint32_t count,
                             Fence* fence = nullptr, uint64_t fenceValue = 0) override;
    void waitIdle() override;

    // --- 帧循环 ---
    void beginFrame() override;
    CommandList* frameCommandList() override;
    void submitAndPresent(SwapChain* swapchain) override;
    void submitOffscreen() override;

    // --- Vulkan 特有访问器 ---
    vk::Instance         vkInstance()          const { return m_instance; }
    vk::PhysicalDevice   vkPhysicalDevice()    const { return m_physicalDevice; }
    vk::Device           vkDevice()            const { return m_device; }
    vk::Queue            graphicsQueue()       const { return m_graphicsQueue; }
    uint32_t             graphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VmaAllocator         vmaAllocator()        const { return m_allocator; }

    VKUploadContext&       uploadContext()       { return *m_uploadContext; }
    VKDescriptorAllocator& descriptorAllocator() { return *m_descriptorAllocators[m_currentFrame]; }
    VKFrameContext&        currentFrameContext() { return *m_frameContexts[m_currentFrame]; }
    uint32_t               currentFrameIndex()   const { return m_currentFrame; }

    // --- RenderPass Cache ---
    vk::RenderPass getOrCreateRenderPass(
        std::span<const TextureFormat> colorFormats,
        TextureFormat depthFormat,
        bool depthEnable,
        vk::AttachmentLoadOp colorLoadOp = vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp colorStoreOp = vk::AttachmentStoreOp::eStore,
        vk::ImageLayout colorFinalLayout = vk::ImageLayout::eColorAttachmentOptimal);

    // --- Framebuffer Cache ---
    vk::Framebuffer getOrCreateFramebuffer(
        vk::RenderPass renderPass,
        std::span<const vk::ImageView> attachments,
        uint32_t width, uint32_t height);

    /// 清空 Framebuffer Cache（resize 时调用）
    void clearFramebufferCache();

private:
    void init(const CreateInfo& ci);
    void shutdown();
    void pickPhysicalDevice(const std::vector<vk::PhysicalDevice>& devices);
    void createLogicalDevice(bool enableValidation);
    vk::SurfaceKHR createSurface(const NativeWindowHandle& window);
    void initFrameContexts(uint32_t count);

    // --- Vulkan 核心 ---
    vk::Instance                m_instance;
    vk::DebugUtilsMessengerEXT  m_debugMessenger;
    vk::PhysicalDevice          m_physicalDevice;
    vk::Device                  m_device;
    vk::SurfaceKHR              m_surface;
    vk::Queue                   m_graphicsQueue;
    vk::Queue                   m_presentQueue;
    VmaAllocator                m_allocator = nullptr;

    uint32_t                    m_graphicsQueueFamily = 0;
    uint32_t                    m_presentQueueFamily  = 0;
    NativeWindowHandle          m_nativeWindow;
    RenderConfig                m_renderConfig;

    GPUDeviceCapabilities          m_caps;
    std::vector<VKSwapChain*>   m_swapChains;

    // --- 私有组件 ---
    std::unique_ptr<VKUploadContext>             m_uploadContext;
    std::vector<std::unique_ptr<VKFrameContext>> m_frameContexts;
    std::vector<std::unique_ptr<VKDescriptorAllocator>> m_descriptorAllocators; // per-frame
    std::vector<std::unique_ptr<VKDescriptorAllocator>> m_standaloneAllocators; // 独立 cmd list
    std::unique_ptr<VKCommandList>               m_frameCmdList;

    uint32_t                    m_frameCount   = 2;
    uint32_t                    m_currentFrame = 0;

    // per-swapchain-image 的信号量，替代 per-frame 的 renderFinished
    // 按 acquired image index 索引，解决 present 异步持有信号量的问题
    std::vector<vk::Semaphore>  m_renderFinishedSemaphores;
    uint32_t                    m_acquiredImageIndex = 0;

    // --- RenderPass Cache ---
    struct RenderPassKey {
        std::array<TextureFormat, 8> colorFormats{};
        uint8_t                      colorCount  = 0;
        TextureFormat                depthFormat = TextureFormat::Unknown;
        bool                         depthEnable = false;
        vk::AttachmentLoadOp         colorLoadOp    = vk::AttachmentLoadOp::eClear;
        vk::AttachmentStoreOp        colorStoreOp   = vk::AttachmentStoreOp::eStore;
        vk::ImageLayout              colorFinalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        bool operator==(const RenderPassKey&) const = default;
    };
    struct RenderPassKeyHash {
        size_t operator()(const RenderPassKey& k) const noexcept;
    };
    std::unordered_map<RenderPassKey, vk::RenderPass, RenderPassKeyHash> m_renderPassCache;

    // --- Framebuffer Cache ---
    struct FramebufferKey {
        vk::RenderPass                    renderPass = nullptr;
        std::array<vk::ImageView, 9>      attachments{}; // 8 color + 1 depth
        uint8_t                           attachmentCount = 0;
        uint32_t                          width  = 0;
        uint32_t                          height = 0;

        bool operator==(const FramebufferKey&) const = default;
    };
    struct FramebufferKeyHash {
        size_t operator()(const FramebufferKey& k) const noexcept;
    };
    std::unordered_map<FramebufferKey, vk::Framebuffer, FramebufferKeyHash> m_framebufferCache;
};

} // namespace mulan::Engine
