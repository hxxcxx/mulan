#include "detail/vk_frame_scheduler.h"

#include "detail/vk_swap_chain.h"
#include "../rhi/engine_error_code.h"
#include <mulan/core/result/error.h>
#include <mulan/core/log/log.h>

namespace mulan::engine {

VKFrameScheduler::VKFrameScheduler(vk::Device device, vk::Queue graphicsQueue, uint32_t graphicsQueueFamily,
                                   VmaAllocator allocator, uint32_t uniformAlignment, uint32_t maxUniformSize)
    : device_(device),
      graphics_queue_(graphicsQueue),
      graphics_queue_family_(graphicsQueueFamily),
      allocator_(allocator),
      uniform_alignment_(uniformAlignment),
      max_uniform_size_(maxUniformSize) {
}

VKFrameScheduler::~VKFrameScheduler() = default;

void VKFrameScheduler::initFrameContexts(uint32_t count) {
    frame_contexts_.clear();
    frame_count_ = count;
    for (uint32_t i = 0; i < count; ++i) {
        frame_contexts_.push_back(std::make_unique<VKFrameContext>(device_, graphics_queue_family_, allocator_,
                                                                   uniform_alignment_, max_uniform_size_));
    }

    descriptor_allocators_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        descriptor_allocators_.push_back(std::make_unique<VKDescriptorAllocator>(device_));
    }

    current_frame_ = 0;
    frame_cmd_list_ = std::make_unique<VKCommandList>(device_, currentFrameContext().cmdBuffer(),
                                                      descriptor_allocators_[current_frame_].get(),
                                                      currentFrameContext().transientUniformArena());
}

Result<std::unique_ptr<CommandList>> VKFrameScheduler::createStandaloneCommandList() {
    return VKCommandList::create(device_, graphics_queue_family_, allocator_, uniform_alignment_, max_uniform_size_);
}

ResultVoid VKFrameScheduler::beginFrame(SwapChain* swapchain) {
    frame_ready_ = false;
    submitted_ = false;
    pending_render_finished_ = nullptr;

    auto& frame = currentFrameContext();
    try {
        frame.waitForFence();
        frame.resetCommandBuffer();

        descriptor_allocators_[current_frame_]->resetPools();
        ++frame_token_;

        frame_cmd_list_ = std::make_unique<VKCommandList>(device_, frame.cmdBuffer(),
                                                          descriptor_allocators_[current_frame_].get(),
                                                          frame.transientUniformArena());
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, error.what()));
    }

    if (swapchain) {
        auto* sc = static_cast<VKSwapChain*>(swapchain);
        if (!sc->acquireNextImage(frame.imageAvailable())) {
            return std::unexpected(
                    makeError(EngineErrorCode::PresentationFailed, "Vulkan swapchain image acquisition failed"));
        }
        pending_render_finished_ = sc->currentRenderFinishedSemaphore();
        if (!pending_render_finished_) {
            return std::unexpected(makeError(EngineErrorCode::PresentationFailed,
                                             "Vulkan swapchain presentation semaphore is unavailable"));
        }
    }
    frame_ready_ = true;
    return {};
}

CommandList* VKFrameScheduler::frameCommandList() {
    if (!frame_ready_)
        return nullptr;
    if (frame_cmd_list_) {
        frame_cmd_list_->setFrameToken(frame_token_);
    }
    return frame_cmd_list_.get();
}

void VKFrameScheduler::markSubmitted(SubmissionToken token) {
    if (frame_cmd_list_)
        frame_cmd_list_->markSubmitted(token);
}

bool VKFrameScheduler::submit(vk::Semaphore completionSemaphore, uint64_t completionValue) {
    if (!frame_ready_) {
        LOG_WARN("[Vulkan] Frame submission skipped because no frame is ready");
        return false;
    }
    auto& frame = currentFrameContext();
    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = frame_cmd_list_->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    vk::Semaphore waitSemaphores[] = { frame.imageAvailable() };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    const vk::Semaphore signalSemaphores[] = { pending_render_finished_, completionSemaphore };
    const uint64_t signalValues[] = { 0, completionValue };
    vk::TimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.signalSemaphoreValueCount = 2;
    timelineInfo.pSignalSemaphoreValues = signalValues;
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = signalSemaphores;
    submitInfo.pNext = &timelineInfo;

    frame.resetFence();
    try {
        graphics_queue_.submit(submitInfo, frame.inFlightFence());
        submitted_ = true;
        return true;
    } catch (const vk::Error& error) {
        // reset 后 submit 失败会留下永久未 signal 的 fence；恢复为 signaled，
        // 保证下一次复用该 FrameContext 时不会死锁。
        frame.restoreSignaledFence();
        LOG_ERROR("[Vulkan] Frame submission failed: {}", error.what());
        return false;
    }
}

ResultVoid VKFrameScheduler::present(SwapChain* swapchain) {
    auto* vkSC = static_cast<VKSwapChain*>(swapchain);
    ResultVoid result;
    if (submitted_ && pending_render_finished_) {
        result = vkSC->presentWithSemaphores(pending_render_finished_);
    } else {
        result = std::unexpected(
                makeError(EngineErrorCode::PresentationFailed, "Vulkan frame submission did not complete"));
    }
    frame_ready_ = false;
    submitted_ = false;
    pending_render_finished_ = nullptr;
    current_frame_ = (current_frame_ + 1) % frame_count_;
    return result;
}

bool VKFrameScheduler::submitOffscreen(vk::Semaphore completionSemaphore, uint64_t completionValue) {
    if (!frame_ready_) {
        LOG_WARN("[Vulkan] Offscreen submission skipped because no frame is ready");
        return false;
    }
    auto& frame = currentFrameContext();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = frame_cmd_list_->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    vk::TimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &completionValue;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &completionSemaphore;
    submitInfo.pNext = &timelineInfo;

    frame.resetFence();
    bool success = false;
    try {
        graphics_queue_.submit(submitInfo, frame.inFlightFence());
        success = true;
    } catch (const vk::Error& error) {
        frame.restoreSignaledFence();
        LOG_ERROR("[Vulkan] Offscreen submission failed: {}", error.what());
    }
    frame_ready_ = false;
    current_frame_ = (current_frame_ + 1) % frame_count_;
    return success;
}

VKDescriptorAllocator& VKFrameScheduler::descriptorAllocator() {
    return *descriptor_allocators_[current_frame_];
}

VKFrameContext& VKFrameScheduler::currentFrameContext() {
    return *frame_contexts_[current_frame_];
}

}  // namespace mulan::engine
