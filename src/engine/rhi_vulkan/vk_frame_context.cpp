#include "vk_frame_context.h"

namespace mulan::engine {

VKFrameContext::VKFrameContext(vk::Device device, uint32_t queueFamily) : device_(device) {
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCI.queueFamilyIndex = queueFamily;
    cmd_pool_ = device_.createCommandPool(poolCI);

    vk::CommandBufferAllocateInfo allocCI;
    allocCI.commandPool = cmd_pool_;
    allocCI.level = vk::CommandBufferLevel::ePrimary;
    allocCI.commandBufferCount = 1;
    auto bufs = device_.allocateCommandBuffers(allocCI);
    cmd_buffer_ = bufs[0];

    image_available_ = device_.createSemaphore({});
    render_finished_ = device_.createSemaphore({});

    vk::FenceCreateInfo fenceCI;
    fenceCI.flags = vk::FenceCreateFlagBits::eSignaled;
    in_flight_fence_ = device_.createFence(fenceCI);
}

VKFrameContext::~VKFrameContext() {
    if (in_flight_fence_)
        device_.destroyFence(in_flight_fence_);
    if (render_finished_)
        device_.destroySemaphore(render_finished_);
    if (image_available_)
        device_.destroySemaphore(image_available_);
    if (cmd_pool_) {
        device_.destroyCommandPool(cmd_pool_);
    }
}

void VKFrameContext::waitForFence() {
    device_.waitForFences(in_flight_fence_, true, UINT64_MAX);
}

void VKFrameContext::resetFence() {
    device_.resetFences(in_flight_fence_);
}

void VKFrameContext::resetCommandBuffer() {
    device_.resetCommandPool(cmd_pool_);
}

}  // namespace mulan::engine
