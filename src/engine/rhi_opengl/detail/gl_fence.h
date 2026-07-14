/**
 * @file gl_fence.h
 * @brief OpenGL 同步栅栏实现
 * @author hxxcxx
 * @date 2026-07-12
 */

#pragma once

#include "gl_common.h"
#include "../../rhi/fence.h"

namespace mulan::engine {

class GLFence final : public Fence {
public:
    explicit GLFence(uint64_t initialValue = 0);
    ~GLFence() override;

    core::Result<void> signal(uint64_t value) override;
    core::Result<void> wait(uint64_t value) override;
    uint64_t completedValue() const override;
    bool isValid() const { return sync_ != nullptr; }

private:
    mutable GLsync sync_ = nullptr;
    uint64_t signaled_value_ = 0;
    mutable uint64_t completed_value_ = 0;
};

}  // namespace mulan::engine
