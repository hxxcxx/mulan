/**
 * @file vk_fence.h
 * @brief Vulkan栅栏实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../fence.h"
#include "vk_convert.h"

namespace mulan::engine {

class VKFence : public Fence {
public:
    VKFence(vk::Device device, uint64_t initialValue);
    ~VKFence();

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

    vk::Semaphore semaphore() const { return semaphore_; }

private:
    vk::Device    device_;
    vk::Semaphore semaphore_;
};

} // namespace mulan::engine
