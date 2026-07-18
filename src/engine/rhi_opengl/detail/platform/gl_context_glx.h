/**
 * @file gl_context_glx.h
 * @brief Linux X11/GLX OpenGL 上下文实现
 * @author hxxcxx
 * @date 2026-07-18
 */

#pragma once

#include "../gl_context.h"

#include <GL/glx.h>
#include <X11/Xlib.h>

namespace mulan::engine {

class X11GLContext final : public GLContext {
public:
    static Result<std::unique_ptr<X11GLContext>> create(const GLContextCreateInfo& ci);
    ~X11GLContext() override;

    bool isValid() const override { return display_ != nullptr && window_ != 0 && context_ != nullptr; }
    bool makeCurrent() override;
    void clearCurrent() override;
    bool swapBuffers() override;
    void setSwapInterval(int interval) override;

private:
    using CreateContextAttribsProc = ::GLXContext (*)(Display*, GLXFBConfig, ::GLXContext, Bool, const int*);
    using SwapIntervalExtProc = void (*)(Display*, GLXDrawable, int);
    using SwapIntervalMesaProc = int (*)(unsigned int);
    using SwapIntervalSgiProc = int (*)(int);

    X11GLContext() = default;
    bool initialize(const GLContextCreateInfo& ci);
    void shutdown();

    Display* display_ = nullptr;
    ::Window window_ = 0;
    ::GLXContext context_ = nullptr;
    SwapIntervalExtProc swap_interval_ext_ = nullptr;
    SwapIntervalMesaProc swap_interval_mesa_ = nullptr;
    SwapIntervalSgiProc swap_interval_sgi_ = nullptr;
};

}  // namespace mulan::engine
