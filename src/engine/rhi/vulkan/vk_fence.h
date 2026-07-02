/**
 * @file vk_fence.h
 * @brief Vulkan栅栏实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../fence.h"
#include "vk_convert.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class VKFence : public Fence {
public:
    /// 创建 VKFence（timeline semaphore）。失败返回 FenceCreateFailed。
    static std::expected<std::unique_ptr<VKFence>, core::Error>
        create(vk::Device device, uint64_t initialValue);
    ~VKFence();

    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
    uint64_t completedValue() const override;

    vk::Semaphore semaphore() const { return semaphore_; }

private:
    VKFence(vk::Device device) : device_(device) {}

    vk::Device    device_;
    vk::Semaphore semaphore_;
};

} // namespace mulan::engine
