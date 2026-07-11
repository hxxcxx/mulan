/**
 * @file vk_frame_context.h
 * @brief Vulkan帧上下文，每帧独立的同步与命令资源
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "vk_common.h"

namespace mulan::engine {

class VKFrameContext {
public:
    VKFrameContext(vk::Device device, uint32_t queueFamily);
    ~VKFrameContext();

    VKFrameContext(const VKFrameContext&) = delete;
    VKFrameContext& operator=(const VKFrameContext&) = delete;

    void waitForFence();
    void resetFence();
    void resetCommandBuffer();

    vk::CommandBuffer cmdBuffer() const { return cmd_buffer_; }
    vk::CommandPool cmdPool() const { return cmd_pool_; }
    vk::Semaphore imageAvailable() const { return image_available_; }
    vk::Semaphore renderFinished() const { return render_finished_; }
    vk::Fence inFlightFence() const { return in_flight_fence_; }

private:
    vk::Device device_;
    vk::CommandPool cmd_pool_;
    vk::CommandBuffer cmd_buffer_;
    vk::Semaphore image_available_;
    vk::Semaphore render_finished_;
    vk::Fence in_flight_fence_;
};

}  // namespace mulan::engine
