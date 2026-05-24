/**
 * @file VKFrameContext.h
 * @brief Vulkan帧上下文，每帧独立的同步与命令资源
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "VkCommon.h"

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

    vk::CommandBuffer  cmdBuffer()       const { return m_cmdBuffer; }
    vk::CommandPool    cmdPool()         const { return m_cmdPool; }
    vk::Semaphore      imageAvailable()  const { return m_imageAvailable; }
    vk::Semaphore      renderFinished()  const { return m_renderFinished; }
    vk::Fence          inFlightFence()   const { return m_inFlightFence; }

private:
    vk::Device         m_device;
    vk::CommandPool    m_cmdPool;
    vk::CommandBuffer  m_cmdBuffer;
    vk::Semaphore      m_imageAvailable;
    vk::Semaphore      m_renderFinished;
    vk::Fence          m_inFlightFence;
};

} // namespace mulan::Engine
