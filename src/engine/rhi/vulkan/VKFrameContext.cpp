#include "VKFrameContext.h"

namespace mulan::engine {

VKFrameContext::VKFrameContext(vk::Device device, uint32_t queueFamily)
    : m_device(device)
{
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCI.queueFamilyIndex = queueFamily;
    m_cmdPool = m_device.createCommandPool(poolCI);

    vk::CommandBufferAllocateInfo allocCI;
    allocCI.commandPool        = m_cmdPool;
    allocCI.level              = vk::CommandBufferLevel::ePrimary;
    allocCI.commandBufferCount = 1;
    auto bufs = m_device.allocateCommandBuffers(allocCI);
    m_cmdBuffer = bufs[0];

    m_imageAvailable = m_device.createSemaphore({});
    m_renderFinished = m_device.createSemaphore({});

    vk::FenceCreateInfo fenceCI;
    fenceCI.flags = vk::FenceCreateFlagBits::eSignaled;
    m_inFlightFence = m_device.createFence(fenceCI);
}

VKFrameContext::~VKFrameContext() {
    if (m_inFlightFence) m_device.destroyFence(m_inFlightFence);
    if (m_renderFinished) m_device.destroySemaphore(m_renderFinished);
    if (m_imageAvailable) m_device.destroySemaphore(m_imageAvailable);
    if (m_cmdPool) {
        m_device.destroyCommandPool(m_cmdPool);
    }
}

void VKFrameContext::waitForFence() {
    m_device.waitForFences(m_inFlightFence, true, UINT64_MAX);
}

void VKFrameContext::resetFence() {
    m_device.resetFences(m_inFlightFence);
}

void VKFrameContext::resetCommandBuffer() {
    m_device.resetCommandPool(m_cmdPool);
}

} // namespace mulan::Engine
