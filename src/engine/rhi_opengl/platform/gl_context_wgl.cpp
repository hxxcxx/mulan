/**
 * @file gl_context_wgl.cpp
 * @brief Windows WGL OpenGL 上下文实现
 * @author hxxcxx
 * @date 2026-07-12
 */

#include "gl_context_wgl.h"

#include "../../rhi/engine_error_code.h"

#include <expected>

namespace mulan::engine {

namespace {

using CreateContextAttribsProc = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

}  // namespace

core::Result<std::unique_ptr<WGLContext>> WGLContext::create(const GLContextCreateInfo& ci) {
    auto context = std::unique_ptr<WGLContext>(new WGLContext());
    if (!context->initialize(ci)) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Failed to create WGL context"));
    }
    return context;
}

bool WGLContext::initialize(const GLContextCreateInfo& ci) {
    if (ci.window.type != NativeWindowHandle::Type::Win32 || ci.window.win32.hWnd == 0)
        return false;

    hwnd_ = reinterpret_cast<HWND>(ci.window.win32.hWnd);
    hdc_ = GetDC(hwnd_);
    owns_hdc_ = hdc_ != nullptr;
    if (!hdc_)
        return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = ci.renderConfig.depthBuffer ? 24 : 0;
    pfd.cStencilBits = ci.renderConfig.stencilBuffer ? 8 : 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pixel_format = ChoosePixelFormat(hdc_, &pfd);
    if (!pixel_format || !SetPixelFormat(hdc_, pixel_format, &pfd))
        return false;

    HGLRC temporary = wglCreateContext(hdc_);
    if (!temporary)
        return false;
    if (!wglMakeCurrent(hdc_, temporary)) {
        wglDeleteContext(temporary);
        return false;
    }

    auto create_context = reinterpret_cast<CreateContextAttribsProc>(wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_context) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(temporary);
        return false;
    }

    const int flags = ci.enableValidation ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;
    const int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB,
        static_cast<int>(ci.majorVersion),
        WGL_CONTEXT_MINOR_VERSION_ARB,
        static_cast<int>(ci.minorVersion),
        WGL_CONTEXT_PROFILE_MASK_ARB,
        WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,
        flags,
        0,
    };
    hglrc_ = create_context(hdc_, nullptr, attributes);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temporary);

    if (!hglrc_)
        return false;

    if (!makeCurrent())
        return false;

    swap_interval_proc_ = reinterpret_cast<SwapIntervalProc>(wglGetProcAddress("wglSwapIntervalEXT"));
    setSwapInterval(ci.renderConfig.vsync ? 1 : 0);
    return true;
}

WGLContext::~WGLContext() {
    shutdown();
}

bool WGLContext::makeCurrent() {
    return hdc_ && hglrc_ && wglMakeCurrent(hdc_, hglrc_) == TRUE;
}

void WGLContext::clearCurrent() {
    if (wglGetCurrentContext() == hglrc_)
        wglMakeCurrent(nullptr, nullptr);
}

bool WGLContext::swapBuffers() {
    return hdc_ && SwapBuffers(hdc_) != FALSE;
}

void WGLContext::setSwapInterval(int interval) {
    if (swap_interval_proc_)
        swap_interval_proc_(interval);
}

void WGLContext::shutdown() {
    const bool hadContext = hglrc_ != nullptr || hdc_ != nullptr;
    clearCurrent();
    if (hglrc_) {
        if (!wglDeleteContext(hglrc_)) {
            LOG_ERROR("[OpenGL] WGL context deletion failed: win32Error={}",
                      static_cast<unsigned long>(GetLastError()));
        }
        hglrc_ = nullptr;
    }
    if (owns_hdc_ && hdc_ && hwnd_) {
        if (!ReleaseDC(hwnd_, hdc_)) {
            LOG_WARN("[OpenGL] Window device-context release failed: win32Error={}",
                     static_cast<unsigned long>(GetLastError()));
        }
        hdc_ = nullptr;
    }
    hwnd_ = nullptr;
    owns_hdc_ = false;
    swap_interval_proc_ = nullptr;
    if (hadContext) {
        LOG_DEBUG("[OpenGL] WGL context shut down");
    }
}

core::Result<std::unique_ptr<GLContext>> createGLContext(const GLContextCreateInfo& ci) {
    auto result = WGLContext::create(ci);
    if (!result)
        return std::unexpected(result.error());
    return std::unique_ptr<GLContext>(std::move(*result));
}

}  // namespace mulan::engine
