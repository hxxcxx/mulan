/**
 * @file gl_context_wgl.h
 * @brief Windows WGL OpenGL 上下文实现
 * @author hxxcxx
 * @date 2026-07-12
 */

#pragma once

#include "../gl_common.h"
#include "../gl_context.h"

namespace mulan::engine {

class WGLContext final : public GLContext {
public:
    using SwapIntervalProc = BOOL(WINAPI*)(int);

    static core::Result<std::unique_ptr<WGLContext>> create(const GLContextCreateInfo& ci);
    ~WGLContext() override;

    bool isValid() const override { return hglrc_ != nullptr && hdc_ != nullptr; }
    bool makeCurrent() override;
    void clearCurrent() override;
    bool swapBuffers() override;
    void setSwapInterval(int interval) override;

    HDC hdc() const { return hdc_; }
    HGLRC hglrc() const { return hglrc_; }
    HWND hwnd() const { return hwnd_; }

private:
    WGLContext() = default;
    bool initialize(const GLContextCreateInfo& ci);
    void shutdown();

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    bool owns_hdc_ = false;
    SwapIntervalProc swap_interval_proc_ = nullptr;
};

}  // namespace mulan::engine
