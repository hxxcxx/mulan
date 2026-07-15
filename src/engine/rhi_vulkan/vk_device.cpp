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

Result<std::unique_ptr<Buffer>> VKDevice::createBuffer(const BufferDesc& desc) {
    return resource_factory_->createBuffer(desc);
}

Result<std::unique_ptr<Texture>> VKDevice::createTexture(const TextureDesc& desc) {
    return resource_factory_->createTexture(desc);
}

Result<std::unique_ptr<Shader>> VKDevice::createShader(const ShaderDesc& desc) {
    return resource_factory_->createShader(desc);
}

Result<std::unique_ptr<PipelineState>> VKDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    return resource_factory_->createPipelineState(desc);
}

Result<std::unique_ptr<ComputePipelineState>> VKDevice::createComputePipelineState(const ComputePipelineDesc& desc) {
    return resource_factory_->createComputePipelineState(desc);
}

Result<std::unique_ptr<CommandList>> VKDevice::createCommandList() {
    auto result = frame_scheduler_->createStandaloneCommandList();
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(*this, RHIResourceKind::CommandList, "StandaloneCommandList");
    return result;
}

Result<std::unique_ptr<SwapChain>> VKDevice::createSwapChain(const SwapChainDesc& desc) {
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

Result<std::unique_ptr<Fence>> VKDevice::createFence(uint64_t initialValue) {
    return resource_factory_->createFence(initialValue);
}

Result<std::unique_ptr<BindGroup>> VKDevice::createBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc) {
    const std::string validationError = validateBindGroupDesc(
            layout, desc, { caps_.minUniformBufferOffsetAlignment, caps_.maxUniformBufferBindingSize });
    if (!validationError.empty())
        return std::unexpected(makeError(EngineErrorCode::ResourceCreateFailed, validationError));
    return resource_factory_->createBindGroup(
            layout, desc, { caps_.minUniformBufferOffsetAlignment, caps_.maxUniformBufferBindingSize });
}

ResultVoid VKDevice::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    assertResourceOwned(dst);
    if (auto wait = waitForResourceLastUse(dst); !wait)
        return std::unexpected(wait.error());
    return upload_context_->uploadTexture(static_cast<VKTexture*>(dst), upload);
}

ResultVoid VKDevice::beginUploadBatch() {
    return upload_context_->beginUploadBatch();
}

ResultVoid VKDevice::flushUploadBatch() {
    return upload_context_->flushUploadBatch();
}

Result<std::unique_ptr<RenderTarget>> VKDevice::createRenderTarget(const RenderTargetDesc& desc) {
    if (auto validation = validateRenderTargetDesc(desc, caps_); !validation)
        return std::unexpected(validation.error());
    frame_scheduler_->ensureSwapchainImageSync(1);
    return resource_factory_->createRenderTarget(desc);
}

Result<std::unique_ptr<Sampler>> VKDevice::createSampler(const SamplerDesc& desc) {
    return resource_factory_->createSampler(desc);
}

Result<SubmissionToken> VKDevice::executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence,
                                                      uint64_t fenceValue) {
    if (auto validation = validateCommandListsForSubmission(cmdLists, count); !validation)
        return std::unexpected(validation.error());
    // vulkan-hpp 的 submit() 为异常版：验证层发现的录制错误（缺 sampler、
    // layout 冲突等）会在这一步抛 vk::Error。这里统一转换为 Result，避免
    // 调用方在提交失败后继续等待永远不会被 signal 的 fence。
    // 注意：真正的异步 GPU 执行错误（device lost）在此处查不到，需在
    // waitIdle / 下一帧 fence 检查时发现。
    if (!cmdLists || count == 0)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "Vulkan command list batch is empty"));
    for (uint32_t i = 0; i < count; ++i)
        assertResourceOwned(cmdLists[i]);
    if (fence)
        assertResourceOwned(fence);
    auto submissionLock = lockSubmissionQueue();
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
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "Vulkan submission timeline is unavailable"));
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
        return token;
    } catch (const vk::Error& e) {
        LOG_ERROR("[Vulkan] Queue submission failed: {}", e.what());
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("[Vulkan] Queue submission failed with a non-Vulkan error: {}", e.what());
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, e.what()));
    }
}

ResultVoid VKDevice::waitIdle() {
    try {
        device_.waitIdle();
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, error.what()));
    }
    collectGarbage();
    return {};
}

// ============================================================
// 帧循环
// ============================================================

Result<CommandList*> VKDevice::beginFrame(SwapChain* swapchain) {
    if (swapchain)
        assertResourceOwned(swapchain);
    collectGarbage();
    if (!frame_scheduler_)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Vulkan frame scheduler is unavailable"));
    if (auto result = frame_scheduler_->beginFrame(swapchain); !result)
        return std::unexpected(result.error());
    CommandList* commandList = frame_scheduler_->frameCommandList();
    if (!commandList)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Vulkan frame CommandList is unavailable"));
    if (!commandList->isTracked())
        commandList->trackResource(*this, RHIResourceKind::CommandList, "VulkanFrameCommandList");
    if (auto result = commandList->begin(); !result)
        return std::unexpected(result.error());
    return commandList;
}

Result<SubmissionToken> VKDevice::submitFrame() {
    auto submissionLock = lockSubmissionQueue();
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

Result<SubmissionToken> VKDevice::submitOffscreenFrame() {
    auto submissionLock = lockSubmissionQueue();
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

Result<SubmissionToken> VKDevice::endFrame(SwapChain* swapchain) {
    if (swapchain)
        assertResourceOwned(swapchain);
    CommandList* commandList = frame_scheduler_->frameCommandList();
    if (!commandList)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Vulkan frame CommandList is unavailable"));
    if (auto recording = commandList->end(); !recording)
        return std::unexpected(recording.error());
    auto result = swapchain ? submitFrame() : submitOffscreenFrame();
    if (!result)
        return std::unexpected(result.error());
    if (swapchain) {
        if (auto presentResult = frame_scheduler_->present(swapchain); !presentResult)
            return std::unexpected(presentResult.error());
    }
    return result;
}
}  // namespace mulan::engine
