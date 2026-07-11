#include "detail/vk_frame_scheduler.h"

#include "detail/vk_swap_chain.h"
#include <mulan/core/result/error.h>

namespace mulan::engine {

VKFrameScheduler::VKFrameScheduler(vk::Device device, vk::Queue graphicsQueue, uint32_t graphicsQueueFamily)
    : device_(device), graphics_queue_(graphicsQueue), graphics_queue_family_(graphicsQueueFamily) {
}

void VKFrameScheduler::initFrameContexts(uint32_t count) {
    frame_contexts_.clear();
    frame_count_ = count;
    for (uint32_t i = 0; i < count; ++i) {
        frame_contexts_.push_back(std::make_unique<VKFrameContext>(device_, graphics_queue_family_));
    }

    descriptor_allocators_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        descriptor_allocators_.push_back(std::make_unique<VKDescriptorAllocator>(device_));
    }

    current_frame_ = 0;
    frame_cmd_list_ = std::make_unique<VKCommandList>(device_, currentFrameContext().cmdBuffer(),
                                                      descriptor_allocators_[current_frame_].get());
}

void VKFrameScheduler::ensureSwapchainImageSync(uint32_t imageCount) {
    for (uint32_t i = static_cast<uint32_t>(render_finished_semaphores_.size()); i < imageCount; ++i) {
        render_finished_semaphores_.push_back(device_.createSemaphore({}));
    }
    if (frame_contexts_.empty()) {
        initFrameContexts(frame_count_);
    }
}

core::Result<std::unique_ptr<CommandList>> VKFrameScheduler::createStandaloneCommandList() {
    auto* allocator = new VKDescriptorAllocator(device_);
    auto result = VKCommandList::create(device_, graphics_queue_family_, allocator);
    if (!result) {
        delete allocator;
        return std::unexpected(result.error());
    }
    standalone_allocators_.emplace_back(allocator);
    return result;
}

void VKFrameScheduler::beginFrame(SwapChain* swapchain) {
    auto& frame = currentFrameContext();
    frame.waitForFence();
    frame.resetFence();
    frame.resetCommandBuffer();

    descriptor_allocators_[current_frame_]->resetPools();
    ++frame_token_;

    standalone_allocators_prev_.clear();
    standalone_allocators_prev_ = std::move(standalone_allocators_);
    standalone_allocators_.clear();

    frame_cmd_list_ =
            std::make_unique<VKCommandList>(device_, frame.cmdBuffer(), descriptor_allocators_[current_frame_].get());

    if (swapchain) {
        auto* sc = static_cast<VKSwapChain*>(swapchain);
        sc->acquireNextImage(frame.imageAvailable());
        acquired_image_index_ = sc->currentImageIndex();
    }
}

CommandList* VKFrameScheduler::frameCommandList() {
    if (frame_cmd_list_) {
        frame_cmd_list_->setFrameToken(frame_token_);
    }
    return frame_cmd_list_.get();
}

void VKFrameScheduler::submit() {
    auto& frame = currentFrameContext();
    pending_render_finished_ = render_finished_semaphores_[acquired_image_index_];

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = frame_cmd_list_->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    vk::Semaphore waitSemaphores[] = { frame.imageAvailable() };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &pending_render_finished_;

    graphics_queue_.submit(submitInfo, frame.inFlightFence());
    submitted_ = true;
}

void VKFrameScheduler::present(SwapChain* swapchain) {
    auto* vkSC = static_cast<VKSwapChain*>(swapchain);
    if (submitted_ && pending_render_finished_) {
        vkSC->presentWithSemaphores(pending_render_finished_);
    } else {
        vkSC->present();
    }
    submitted_ = false;
    pending_render_finished_ = nullptr;
    current_frame_ = (current_frame_ + 1) % frame_count_;
}

void VKFrameScheduler::submitOffscreen() {
    auto& frame = currentFrameContext();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    vk::CommandBuffer cmdBuf = frame_cmd_list_->cmdBuffer();
    submitInfo.pCommandBuffers = &cmdBuf;

    graphics_queue_.submit(submitInfo, frame.inFlightFence());
    current_frame_ = (current_frame_ + 1) % frame_count_;
}

VKDescriptorAllocator& VKFrameScheduler::descriptorAllocator() {
    return *descriptor_allocators_[current_frame_];
}

VKFrameContext& VKFrameScheduler::currentFrameContext() {
    return *frame_contexts_[current_frame_];
}

}  // namespace mulan::engine
