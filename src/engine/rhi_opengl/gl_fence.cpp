/**
 * @file gl_fence.cpp
 * @brief OpenGL 同步栅栏实现
 * @author hxxcxx
 * @date 2026-07-12
 */

#include "detail/gl_fence.h"
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

GLFence::GLFence(uint64_t initialValue) : signaled_value_(initialValue), completed_value_(initialValue) {
}

GLFence::~GLFence() {
    if (sync_)
        glDeleteSync(sync_);
}

core::Result<void> GLFence::signal(uint64_t value) {
    if (sync_)
        glDeleteSync(sync_);
    sync_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!sync_)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "glFenceSync failed"));
    signaled_value_ = value;
    return {};
}

core::Result<void> GLFence::wait(uint64_t value) {
    if (value <= completed_value_)
        return {};
    if (!sync_ || value > signaled_value_)
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionWaitFailed, "OpenGL fence value has not been signaled"));

    for (;;) {
        const GLenum result = glClientWaitSync(sync_, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            completed_value_ = signaled_value_;
            return {};
        }
        if (result == GL_WAIT_FAILED)
            return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, "glClientWaitSync failed"));
    }
}

uint64_t GLFence::completedValue() const {
    if (sync_ && completed_value_ < signaled_value_) {
        const GLenum result = glClientWaitSync(sync_, 0, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
            completed_value_ = signaled_value_;
    }
    return completed_value_;
}

}  // namespace mulan::engine
