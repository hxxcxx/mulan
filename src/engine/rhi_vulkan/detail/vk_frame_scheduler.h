/**
 * @file vk_frame_scheduler.h
 * @brief VKFrameScheduler: 管理每帧的 command buffer、frame context、descriptor allocator 等。
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "vk_command_list.h"
#include "vk_descriptor_allocator.h"
#include "vk_frame_context.h"
#include "../rhi/swap_chain.h"

#include <memory>
#include <vector>

namespace mulan::engine {

class VKFrameScheduler {
public:
    VKFrameScheduler(vk::Device device, vk::Queue graphicsQueue, uint32_t graphicsQueueFamily, VmaAllocator allocator,
                     uint32_t uniformAlignment, uint32_t maxUniformSize);
    ~VKFrameScheduler();

    VKFrameScheduler(const VKFrameScheduler&) = delete;
    VKFrameScheduler& operator=(const VKFrameScheduler&) = delete;

    void initFrameContexts(uint32_t count);
    void ensureSwapchainImageSync(uint32_t imageCount);

    Result<std::unique_ptr<CommandList>> createStandaloneCommandList();

    Result<void> beginFrame(SwapChain* swapchain);
    CommandList* frameCommandList();
    void markSubmitted(SubmissionToken token);
    bool submit(vk::Semaphore completionSemaphore, uint64_t completionValue);
    Result<void> present(SwapChain* swapchain);
    bool submitOffscreen(vk::Semaphore completionSemaphore, uint64_t completionValue);

    VKDescriptorAllocator& descriptorAllocator();
    VKFrameContext& currentFrameContext();
    uint32_t currentFrameIndex() const { return current_frame_; }

private:
    vk::Device device_;
    vk::Queue graphics_queue_;
    uint32_t graphics_queue_family_ = 0;
    VmaAllocator allocator_ = nullptr;
    uint32_t uniform_alignment_ = 1;
    uint32_t max_uniform_size_ = 1;

    std::vector<std::unique_ptr<VKFrameContext>> frame_contexts_;
    std::vector<std::unique_ptr<VKDescriptorAllocator>> descriptor_allocators_;
    std::unique_ptr<VKCommandList> frame_cmd_list_;

    uint32_t frame_count_ = 2;
    uint32_t current_frame_ = 0;
    uint64_t frame_token_ = 0;

    std::vector<vk::Semaphore> render_finished_semaphores_;
    uint32_t acquired_image_index_ = 0;
    vk::Semaphore pending_render_finished_ = nullptr;
    bool frame_ready_ = false;
    bool submitted_ = false;
};

}  // namespace mulan::engine
