// VMA 实现（必须在 #include <vk_mem_alloc.h> 之前）
#define VMA_IMPLEMENTATION

#include "detail/vk_device.h"

// Vulkan-Hpp 动态 dispatch 存储（必须在 #include <vulkan/vulkan.hpp> 之后）
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>
#include <array>
#include <string>
#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"
#include "detail/vk_debug_name.h"
#include "detail/vk_bind_group.h"
#include "detail/vk_compute_pipeline.h"

namespace mulan::engine {

// ============================================================
// 资源创建
// ============================================================

core::Result<std::unique_ptr<Buffer>> VKDevice::createBuffer(const BufferDesc& desc) {
    return resource_factory_->createBuffer(desc);
}

core::Result<std::unique_ptr<Texture>> VKDevice::createTexture(const TextureDesc& desc) {
    return resource_factory_->createTexture(desc);
}

core::Result<std::unique_ptr<Shader>> VKDevice::createShader(const ShaderDesc& desc) {
    return resource_factory_->createShader(desc);
}

core::Result<std::unique_ptr<PipelineState>> VKDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    return resource_factory_->createPipelineState(desc);
}

core::Result<std::unique_ptr<ComputePipelineState>> VKDevice::createComputePipelineState(
        const ComputePipelineDesc& desc) {
    return resource_factory_->createComputePipelineState(desc);
}

core::Result<std::unique_ptr<CommandList>> VKDevice::createCommandList() {
    auto result = frame_scheduler_->createStandaloneCommandList();
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(*this, RHIResourceKind::CommandList, "StandaloneCommandList");
    return result;
}

core::Result<std::unique_ptr<SwapChain>> VKDevice::createSwapChain(const SwapChainDesc& desc) {
    if (!desc.window.valid()) {
        return std::unexpected(
                makeError(EngineErrorCode::SwapChainCreateFailed, "Vulkan swap chain requires a native window handle"));
    }

    vk::SurfaceKHR surface;
    try {
        surface = createSurface(desc.window);
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::SwapChainCreateFailed, e.what()));
    }
    if (!surface) {
        return std::unexpected(makeError(EngineErrorCode::SwapChainCreateFailed, "Vulkan surface creation failed"));
    }
    uint32_t presentQueueFamily = UINT32_MAX;
    const auto queueFamilies = physical_device_.getQueueFamilyProperties();
    for (uint32_t family = 0; family < queueFamilies.size(); ++family) {
        if (queueFamilies[family].queueCount != 0 && physical_device_.getSurfaceSupportKHR(family, surface)) {
            presentQueueFamily = family;
            // 优先使用图形队列，避免额外的所有权转移；仍完整支持独立呈现队列。
            if (family == graphics_queue_family_)
                break;
        }
    }
    if (presentQueueFamily == UINT32_MAX) {
        instance_.destroySurfaceKHR(surface);
        return std::unexpected(
                makeError(EngineErrorCode::SurfaceNotSupported, "No Vulkan queue family can present to this surface"));
    }

    VKSwapChain::InitParams params;
    params.instance = instance_;
    params.physicalDevice = physical_device_;
    params.device = device_;
    params.allocator = allocator_;
    params.graphicsQueueFamily = graphics_queue_family_;
    params.presentQueueFamily = presentQueueFamily;
    params.graphicsQueue = graphics_queue_;
    params.presentQueue = device_.getQueue(presentQueueFamily, 0);
    params.surface = surface;

    auto result = VKSwapChain::create(desc, params, render_config_);
    if (!result) {
        instance_.destroySurfaceKHR(surface);
        return std::unexpected(result.error());
    }
    auto& swapchain = *result;

    frame_scheduler_->ensureSwapchainImageSync(swapchain->imageCount());
    (*result)->trackResource(*this, RHIResourceKind::SwapChain, "SwapChain");
    return result;
}

core::Result<std::unique_ptr<Fence>> VKDevice::createFence(uint64_t initialValue) {
    return resource_factory_->createFence(initialValue);
}

core::Result<std::unique_ptr<BindGroup>> VKDevice::createBindGroup(const BindGroupLayout& layout,
                                                                   const BindGroupDesc& desc) {
    return resource_factory_->createBindGroup(layout, desc);
}

void VKDevice::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    upload_context_->uploadTexture(static_cast<VKTexture*>(dst), upload);
}

void VKDevice::beginUploadBatch() {
    upload_context_->beginUploadBatch();
}

void VKDevice::flushUploadBatch() {
    upload_context_->flushUploadBatch();
}

core::Result<std::unique_ptr<RenderTarget>> VKDevice::createRenderTarget(const RenderTargetDesc& desc) {
    frame_scheduler_->ensureSwapchainImageSync(1);
    RenderTargetDesc resolvedDesc = desc;
    if (resolvedDesc.sampleCount > caps_.maxSampleCount)
        resolvedDesc.sampleCount = caps_.maxSampleCount;
    if (resolvedDesc.sampleCount != 1 && resolvedDesc.sampleCount != 2 && resolvedDesc.sampleCount != 4 &&
        resolvedDesc.sampleCount != 8) {
        resolvedDesc.sampleCount = 1;
    }
    return resource_factory_->createRenderTarget(resolvedDesc);
}

core::Result<std::unique_ptr<Sampler>> VKDevice::createSampler(const SamplerDesc& desc) {
    return resource_factory_->createSampler(desc);
}

void VKDevice::executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence, uint64_t fenceValue) {
    // vulkan-hpp 的 submit() 为异常版：验证层发现的录制错误（缺 sampler、
    // layout 冲突等）会在这一步抛 vk::Error。公共签名是 void，异常若逃逸会
    // 变成未捕获崩溃。这里接住并记日志，保持提交失败可见而非静默崩溃。
    // 注意：真正的异步 GPU 执行错误（device lost）在此处查不到，需在
    // waitIdle / 下一帧 fence 检查时发现。
    std::vector<vk::CommandBuffer> cmdBuffers(count);
    for (uint32_t i = 0; i < count; ++i) {
        cmdBuffers[i] = static_cast<VKCommandList*>(cmdLists[i])->cmdBuffer();
    }

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = count;
    submitInfo.pCommandBuffers = cmdBuffers.data();

    const SubmissionToken token = reserveSubmissionToken();
    auto* completionFence = static_cast<VKFence*>(submissionFence());
    if (!token || !completionFence) {
        LOG_ERROR("[Vulkan] Standalone submission timeline is unavailable");
        return;
    }

    std::array<vk::Semaphore, 2> signalSemaphores{};
    std::array<uint64_t, 2> signalValues{};
    uint32_t signalCount = 0;
    if (fence) {
        auto* vkFence = static_cast<VKFence*>(fence);
        signalSemaphores[signalCount] = vkFence->semaphore();
        signalValues[signalCount++] = fenceValue;
    }
    signalSemaphores[signalCount] = completionFence->semaphore();
    signalValues[signalCount++] = token.value;

    vk::TimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.signalSemaphoreValueCount = signalCount;
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    submitInfo.signalSemaphoreCount = signalCount;
    submitInfo.pSignalSemaphores = signalSemaphores.data();
    submitInfo.pNext = &timelineInfo;

    try {
        graphics_queue_.submit(submitInfo);
        for (uint32_t i = 0; i < count; ++i)
            cmdLists[i]->markSubmitted(token);
        commitSubmission(token);
    } catch (const vk::Error& e) {
        LOG_ERROR("[Vulkan] Queue submission failed: {}", e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("[Vulkan] Queue submission failed with a non-Vulkan error: {}", e.what());
    }
}

void VKDevice::waitIdle() {
    device_.waitIdle();
}

// ============================================================
// 帧循环
// ============================================================

void VKDevice::beginFrame(SwapChain* swapchain) {
    collectGarbage();
    frame_scheduler_->beginFrame(swapchain);
}

void VKDevice::clearCaches() {
    // dynamic rendering: 无需 Framebuffer 缓存
}

CommandList* VKDevice::frameCommandList() {
    return frame_scheduler_->frameCommandList();
}

core::Result<SubmissionToken> VKDevice::submitAndPresent(SwapChain* swapchain) {
    auto result = submit();
    if (!result)
        return std::unexpected(result.error());
    present(swapchain);
    return result;
}

core::Result<SubmissionToken> VKDevice::submit() {
    const SubmissionToken token = reserveSubmissionToken();
    if (!token)
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "Vulkan submission timeline is unavailable"));
    auto* completionFence = static_cast<VKFence*>(submissionFence());
    if (!frame_scheduler_->submit(completionFence->semaphore(), token.value))
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "Vulkan frame submission failed"));
    frame_scheduler_->markSubmitted(token);
    commitSubmission(token);
    return token;
}

void VKDevice::present(SwapChain* swapchain) {
    frame_scheduler_->present(swapchain);
}

core::Result<SubmissionToken> VKDevice::submitOffscreen() {
    const SubmissionToken token = reserveSubmissionToken();
    if (!token)
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "Vulkan submission timeline is unavailable"));
    auto* completionFence = static_cast<VKFence*>(submissionFence());
    if (!frame_scheduler_->submitOffscreen(completionFence->semaphore(), token.value))
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "Vulkan offscreen submission failed"));
    frame_scheduler_->markSubmitted(token);
    commitSubmission(token);
    return token;
}
}  // namespace mulan::engine
