/**
 * @file gl_context_glx.cpp
 * @brief Linux X11/GLX OpenGL 上下文实现
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "../detail/platform/gl_context_glx.h"

#include "../../rhi/engine_error_code.h"

#include <mulan/core/log/log.h>

#include <GL/glxext.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <expected>

namespace mulan::engine {

namespace {

GLXFBConfig findWindowFramebufferConfig(Display* display, int screen, VisualID visualId,
                                        const RenderConfig& renderConfig) {
    int configCount = 0;
    GLXFBConfig* configs = glXGetFBConfigs(display, screen, &configCount);
    if (!configs)
        return nullptr;

    GLXFBConfig selected = nullptr;
    int selectedScore = -1;
    for (int index = 0; index < configCount; ++index) {
        XVisualInfo* visualInfo = glXGetVisualFromFBConfig(display, configs[index]);
        const bool visualMatches = visualInfo && visualInfo->visualid == visualId;
        if (visualInfo)
            XFree(visualInfo);
        if (!visualMatches)
            continue;

        int drawableType = 0;
        int renderType = 0;
        int doubleBuffered = 0;
        int depthBits = 0;
        int stencilBits = 0;
        glXGetFBConfigAttrib(display, configs[index], GLX_DRAWABLE_TYPE, &drawableType);
        glXGetFBConfigAttrib(display, configs[index], GLX_RENDER_TYPE, &renderType);
        glXGetFBConfigAttrib(display, configs[index], GLX_DOUBLEBUFFER, &doubleBuffered);
        glXGetFBConfigAttrib(display, configs[index], GLX_DEPTH_SIZE, &depthBits);
        glXGetFBConfigAttrib(display, configs[index], GLX_STENCIL_SIZE, &stencilBits);

        if ((drawableType & GLX_WINDOW_BIT) == 0 || (renderType & GLX_RGBA_BIT) == 0 || !doubleBuffered)
            continue;
        if (renderConfig.depthBuffer && depthBits < 24)
            continue;
        if (renderConfig.stencilBuffer && stencilBits < 8)
            continue;

        const int score = std::min(depthBits, 24) + std::min(stencilBits, 8);
        if (score > selectedScore) {
            selected = configs[index];
            selectedScore = score;
        }
    }

    XFree(configs);
    return selected;
}

template <typename Proc>
Proc loadGLXProc(const char* name) {
    return reinterpret_cast<Proc>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}

}  // namespace

Result<std::unique_ptr<X11GLContext>> X11GLContext::create(const GLContextCreateInfo& ci) {
    auto context = std::unique_ptr<X11GLContext>(new X11GLContext());
    if (!context->initialize(ci)) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Failed to create X11/GLX context"));
    }
    return context;
}

bool X11GLContext::initialize(const GLContextCreateInfo& ci) {
    if (ci.window.type != NativeWindowHandle::Type::XCB || ci.window.xcb.connection == 0 || ci.window.xcb.window == 0)
        return false;

    display_ = XOpenDisplay(nullptr);
    if (!display_)
        return false;

    window_ = static_cast<::Window>(ci.window.xcb.window);
    XWindowAttributes windowAttributes{};
    if (!XGetWindowAttributes(display_, window_, &windowAttributes) || !windowAttributes.visual)
        return false;

    const int screen = XScreenNumberOfScreen(windowAttributes.screen);
    const VisualID visualId = XVisualIDFromVisual(windowAttributes.visual);
    const GLXFBConfig framebufferConfig = findWindowFramebufferConfig(display_, screen, visualId, ci.renderConfig);
    if (!framebufferConfig)
        return false;

    const auto createContext = loadGLXProc<CreateContextAttribsProc>("glXCreateContextAttribsARB");
    if (!createContext)
        return false;

    const int flags = ci.enableValidation ? GLX_CONTEXT_DEBUG_BIT_ARB : 0;
    const int contextAttributes[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB,
        static_cast<int>(ci.majorVersion),
        GLX_CONTEXT_MINOR_VERSION_ARB,
        static_cast<int>(ci.minorVersion),
        GLX_CONTEXT_PROFILE_MASK_ARB,
        GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB,
        flags,
        None,
    };
    context_ = createContext(display_, framebufferConfig, nullptr, True, contextAttributes);
    XSync(display_, False);
    if (!context_ || !makeCurrent())
        return false;

    swap_interval_ext_ = loadGLXProc<SwapIntervalExtProc>("glXSwapIntervalEXT");
    swap_interval_mesa_ = loadGLXProc<SwapIntervalMesaProc>("glXSwapIntervalMESA");
    swap_interval_sgi_ = loadGLXProc<SwapIntervalSgiProc>("glXSwapIntervalSGI");
    setSwapInterval(ci.renderConfig.vsync ? 1 : 0);
    return true;
}

X11GLContext::~X11GLContext() {
    shutdown();
}

bool X11GLContext::makeCurrent() {
    return isValid() && glXMakeCurrent(display_, window_, context_) == True;
}

void X11GLContext::clearCurrent() {
    if (display_ && context_ && glXGetCurrentContext() == context_)
        glXMakeCurrent(display_, None, nullptr);
}

bool X11GLContext::swapBuffers() {
    if (!isValid())
        return false;
    glXSwapBuffers(display_, window_);
    return true;
}

void X11GLContext::setSwapInterval(int interval) {
    if (!isValid())
        return;
    if (swap_interval_ext_) {
        swap_interval_ext_(display_, window_, interval);
    } else if (swap_interval_mesa_) {
        (void) swap_interval_mesa_(static_cast<unsigned int>(std::max(interval, 0)));
    } else if (swap_interval_sgi_ && interval > 0) {
        (void) swap_interval_sgi_(interval);
    }
}

void X11GLContext::shutdown() {
    const bool hadContext = display_ != nullptr || context_ != nullptr;
    clearCurrent();
    if (display_ && context_) {
        glXDestroyContext(display_, context_);
        context_ = nullptr;
    }
    window_ = 0;
    if (display_) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
    swap_interval_ext_ = nullptr;
    swap_interval_mesa_ = nullptr;
    swap_interval_sgi_ = nullptr;
    if (hadContext)
        LOG_DEBUG("[OpenGL] X11/GLX context shut down");
}

Result<std::unique_ptr<GLContext>> createGLContext(const GLContextCreateInfo& ci) {
    auto result = X11GLContext::create(ci);
    if (!result)
        return std::unexpected(result.error());
    return std::unique_ptr<GLContext>(std::move(*result));
}

bool hasCurrentGLContext() noexcept {
    return glXGetCurrentContext() != nullptr;
}

}  // namespace mulan::engine
