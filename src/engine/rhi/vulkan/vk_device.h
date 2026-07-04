/**
 * @file vk_device.h
 * @brief Vulkan设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "vk_common.h"
#include "../device.h"
#include "../../window.h"
#include "vk_convert.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "vk_shader.h"
#include "vk_pipeline_state.h"
#include "vk_command_list.h"
#include "vk_swap_chain.h"
#include "vk_render_target.h"
#include "vk_sampler.h"
#include "vk_fence.h"
#include "vk_upload_context.h"
#include "vk_frame_context.h"
#include "vk_descriptor_allocator.h"

#include <vector>
#include <memory>
#include <set>
#include <array>
#include <span>
#include <unordered_map>

namespace mulan::engine {

class VKDevice : public RHIDevice {
public:
    explicit VKDevice(const DeviceCreateInfo& ci);
    ~VKDevice();

    // --- Device 信息 ---
    GraphicsBackend backend() const override;
    const GPUDeviceCapabilities& capabilities() const override;
    const RenderConfig& renderConfig() const override;
    math::Mat4 clipSpaceCorrectionMatrix() const override;

    // --- 资源创建 ---
    std::expected<std::unique_ptr<Buffer>,        core::Error> createBuffer(const BufferDesc& desc) override;
    std::expected<std::unique_ptr<Texture>,       core::Error> createTexture(const TextureDesc& desc) override;
    std::expected<std::unique_ptr<Shader>,        core::Error> createShader(const ShaderDesc& desc) override;
    std::expected<std::unique_ptr<PipelineState>, core::Error> createPipelineState(const GraphicsPipelineDesc& desc) override;
    std::expected<std::unique_ptr<CommandList>,   core::Error> createCommandList() override;
    std::expected<std::unique_ptr<SwapChain>,     core::Error> createSwapChain(const SwapChainDesc& desc) override;
    std::expected<std::unique_ptr<RenderTarget>,  core::Error> createRenderTarget(const RenderTargetDesc& desc) override;
    std::expected<std::unique_ptr<Sampler>,       core::Error> createSampler(const SamplerDesc& desc) override;
    std::expected<std::unique_ptr<Fence>,         core::Error> createFence(uint64_t initialValue = 0) override;
    std::expected<std::unique_ptr<BindGroup>,     core::Error> createBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc) override;

    // --- 资源上传 ---
    void uploadTextureData(Texture* dst, const void* data,
                           uint32_t width, uint32_t height,
                           TextureFormat format) override;

    // --- 提交命令 ---
    void executeCommandLists(CommandList** cmdLists, uint32_t count,
                             Fence* fence = nullptr, uint64_t fenceValue = 0) override;
    void waitIdle() override;

    // --- 帧循环 ---
    void beginFrame(SwapChain* swapchain = nullptr) override;
    void clearCaches() override;
    CommandList* frameCommandList() override;
    void submitAndPresent(SwapChain* swapchain) override;
    void submit() override;
    void present(SwapChain* swapchain) override;
    void submitOffscreen() override;

    // --- Vulkan 特有访问器 ---
    vk::Instance         vkInstance()          const { return instance_; }
    vk::PhysicalDevice   vkPhysicalDevice()    const { return physical_device_; }
    vk::Device           vkDevice()            const { return device_; }
    vk::Queue            graphicsQueue()       const { return graphics_queue_; }
    uint32_t             graphicsQueueFamily() const { return graphics_queue_family_; }
    VmaAllocator         vmaAllocator()        const { return allocator_; }

    VKUploadContext&       uploadContext()       { return *upload_context_; }
    VKDescriptorAllocator& descriptorAllocator() { return *descriptor_allocators_[current_frame_]; }
    VKFrameContext&        currentFrameContext() { return *frame_contexts_[current_frame_]; }
    uint32_t               currentFrameIndex()   const { return current_frame_; }

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
    void init(const DeviceCreateInfo& ci);
    void shutdown();
    void pickPhysicalDevice(const std::vector<vk::PhysicalDevice>& devices);
    void createLogicalDevice(bool enableValidation);
    vk::SurfaceKHR createSurface(const NativeWindowHandle& window);
    void initFrameContexts(uint32_t count);

    // --- Vulkan 核心 ---
    vk::Instance                instance_;
    vk::DebugUtilsMessengerEXT  debug_messenger_;
    vk::PhysicalDevice          physical_device_;
    vk::Device                  device_;
    vk::SurfaceKHR              surface_;
    vk::Queue                   graphics_queue_;
    vk::Queue                   present_queue_;
    VmaAllocator                allocator_ = nullptr;

    uint32_t                    graphics_queue_family_ = 0;
    uint32_t                    present_queue_family_  = 0;
    NativeWindowHandle          native_window_;
    RenderConfig                render_config_;

    GPUDeviceCapabilities          caps_;

    // --- 私有组件 ---
    std::unique_ptr<VKUploadContext>             upload_context_;
    std::vector<std::unique_ptr<VKFrameContext>> frame_contexts_;
    std::vector<std::unique_ptr<VKDescriptorAllocator>> descriptor_allocators_; // per-frame
    std::vector<std::unique_ptr<VKDescriptorAllocator>> standalone_allocators_; // 独立 cmd list
    std::unique_ptr<VKCommandList>               frame_cmd_list_;

    uint32_t                    frame_count_   = 2;
    uint32_t                    current_frame_ = 0;

    // per-swapchain-image 的信号量，替代 per-frame 的 renderFinished
    // 按 acquired image index 索引，解决 present 异步持有信号量的问题
    std::vector<vk::Semaphore>  render_finished_semaphores_;
    uint32_t                    acquired_image_index_ = 0;

    // 分离 submit/present 所需状态
    vk::Semaphore               pending_render_finished_ = nullptr;
    bool                        submitted_ = false;

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
    std::unordered_map<RenderPassKey, vk::RenderPass, RenderPassKeyHash> render_pass_cache_;

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
    std::unordered_map<FramebufferKey, vk::Framebuffer, FramebufferKeyHash> framebuffer_cache_;
};

} // namespace mulan::engine
