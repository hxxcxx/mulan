/**
 * @file vk_device.h
 * @brief Vulkan设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "vk_common.h"
#include "../rhi/device.h"
#include "../rhi/window.h"
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
#include "vk_frame_scheduler.h"
#include "vk_resource_factory.h"

#include <vector>
#include <memory>
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
    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) override;
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override;
    Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc) override;
    Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc) override;
    Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(const ComputePipelineDesc& desc) override;
    Result<std::unique_ptr<CommandList>> createCommandList() override;
    Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc& desc) override;
    Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc& desc) override;
    Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc& desc) override;
    Result<std::unique_ptr<Fence>> createFence(uint64_t initialValue = 0) override;
    Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout& layout,
                                                       const BindGroupDesc& desc) override;

    // --- 资源上传 ---
    Result<void> uploadTextureData(Texture* dst, const TextureUploadDesc& upload) override;
    Result<void> beginUploadBatch() override;
    Result<void> flushUploadBatch() override;

    // --- 提交命令 ---
    Result<SubmissionToken> executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence = nullptr,
                                                uint64_t fenceValue = 0) override;
    Result<void> waitIdle() override;

    // --- 帧循环 ---
    Result<CommandList*> beginFrame(SwapChain* swapchain = nullptr) override;
    Result<SubmissionToken> endFrame(SwapChain* swapchain = nullptr) override;

    // --- Vulkan 特有访问器 ---
    vk::Instance vkInstance() const { return instance_; }
    vk::PhysicalDevice vkPhysicalDevice() const { return physical_device_; }
    vk::Device vkDevice() const { return device_; }
    vk::Queue graphicsQueue() const { return graphics_queue_; }
    uint32_t graphicsQueueFamily() const { return graphics_queue_family_; }
    VmaAllocator vmaAllocator() const { return allocator_; }

    VKUploadContext& uploadContext() { return *upload_context_; }
    VKDescriptorAllocator& descriptorAllocator() { return frame_scheduler_->descriptorAllocator(); }
    VKFrameContext& currentFrameContext() { return frame_scheduler_->currentFrameContext(); }
    uint32_t currentFrameIndex() const { return frame_scheduler_->currentFrameIndex(); }

private:
    Result<SubmissionToken> submitFrame();
    Result<SubmissionToken> submitOffscreenFrame();
    void init(const DeviceCreateInfo& ci);
    void shutdown();
    void pickPhysicalDevice(const std::vector<vk::PhysicalDevice>& devices);
    void createLogicalDevice(bool enableValidation);
    vk::SurfaceKHR createSurface(const NativeWindowHandle& window);

    // --- Vulkan 核心 ---
    vk::Instance instance_;
    vk::DebugUtilsMessengerEXT debug_messenger_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    vk::Queue graphics_queue_;
    VmaAllocator allocator_ = nullptr;

    uint32_t graphics_queue_family_ = 0;
    RenderConfig render_config_;

    GPUDeviceCapabilities caps_;

    // --- 私有组件 ---
    std::unique_ptr<VKUploadContext> upload_context_;
    std::unique_ptr<VKFrameScheduler> frame_scheduler_;
    std::unique_ptr<VKResourceFactory> resource_factory_;
};

}  // namespace mulan::engine
