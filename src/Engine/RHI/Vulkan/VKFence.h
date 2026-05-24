/**
 * @file VKFence.h
 * @brief Vulkan栅栏实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../Fence.h"
#include "VkConvert.h"

namespace MulanGeo::engine {

class VKFence : public Fence {
public:
    VKFence(vk::Device device, uint64_t initialValue);
    ~VKFence();

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

    vk::Semaphore semaphore() const { return m_semaphore; }

private:
    vk::Device    m_device;
    vk::Semaphore m_semaphore;
};

} // namespace MulanGeo::Engine
