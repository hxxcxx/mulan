#include "detail/vk_frame_context.h"

namespace mulan::engine {

VKFrameContext::VKFrameContext(vk::Device device, uint32_t queueFamily, VmaAllocator allocator,
                               uint32_t uniformAlignment, uint32_t maxUniformSize)
    : device_(device), transient_uniform_arena_(allocator, uniformAlignment, maxUniformSize) {
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

    vk::FenceCreateInfo fenceCI;
    fenceCI.flags = vk::FenceCreateFlagBits::eSignaled;
    in_flight_fence_ = device_.createFence(fenceCI);
}

VKFrameContext::~VKFrameContext() {
    if (in_flight_fence_)
        device_.destroyFence(in_flight_fence_);
    if (image_available_)
        device_.destroySemaphore(image_available_);
    if (cmd_pool_) {
        device_.destroyCommandPool(cmd_pool_);
    }
}

void VKFrameContext::waitForFence() {
    (void) device_.waitForFences(in_flight_fence_, true, UINT64_MAX);
}

void VKFrameContext::resetFence() {
    (void) device_.resetFences(in_flight_fence_);
}

void VKFrameContext::restoreSignaledFence() {
    if (in_flight_fence_)
        device_.destroyFence(in_flight_fence_);
    vk::FenceCreateInfo fenceCI;
    fenceCI.flags = vk::FenceCreateFlagBits::eSignaled;
    in_flight_fence_ = device_.createFence(fenceCI);
}

void VKFrameContext::resetCommandBuffer() {
    (void) device_.resetCommandPool(cmd_pool_);
}

}  // namespace mulan::engine
