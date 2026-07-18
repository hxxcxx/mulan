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

Result<std::unique_ptr<GLContext>> createGLContext(const GLContextCreateInfo& ci);

/**
 * @brief 查询调用线程是否绑定了当前平台的 OpenGL 上下文
 *
 * 通用命令实现不得直接依赖 WGL 或 GLX；平台源文件负责提供对应查询。
 */
bool hasCurrentGLContext() noexcept;

}  // namespace mulan::engine
