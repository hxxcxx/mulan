/**
 * @file gl_context.h
 * @brief OpenGL 平台上下文抽象，隔离 WGL/GLX/EGL 与通用渲染代码
 * @author hxxcxx
 * @date 2026-07-12
 */

#pragma once

#include "../../rhi/window.h"
#include <mulan/core/result/error.h>

#include <cstdint>
#include <memory>

namespace mulan::engine {

struct GLContextCreateInfo {
    NativeWindowHandle window;
    RenderConfig renderConfig;
    bool enableValidation = true;
    uint32_t majorVersion = 4;
    uint32_t minorVersion = 6;
};

class GLContext {
public:
    virtual ~GLContext() = default;

    virtual bool isValid() const = 0;
    virtual bool makeCurrent() = 0;
    virtual void clearCurrent() = 0;
    virtual bool swapBuffers() = 0;
    virtual void setSwapInterval(int interval) = 0;

protected:
    GLContext() = default;
    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;
};

core::Result<std::unique_ptr<GLContext>> createGLContext(const GLContextCreateInfo& ci);

}  // namespace mulan::engine
