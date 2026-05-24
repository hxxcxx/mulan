/**
 * @file GLDevice.cpp
 * @brief OpenGL 设备实现 — 上下文创建与资源工厂
 * @author terry
 * @date 2026-04-16
 */

#include "GLDevice.h"
#include "GLSwapChain.h"
#include "GLShader.h"
#include "GLPipelineState.h"
#include "GLBuffer.h"
#include "GLCommandList.h"
#include "GLRenderTarget.h"
#include "GLTexture.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace mulan::engine {

namespace {
bool isExtensionSupported(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && std::strcmp(ext, name) == 0) return true;
    }
    return false;
}
} // anonymous namespace

// ============================================================
// 初始化 / 销毁
// ============================================================

void GLDevice::init(const CreateInfo& ci) {
    m_nativeWindow  = ci.window;
    m_renderConfig  = ci.renderConfig;

#ifdef _WIN32
    if (ci.window.type == NativeWindowHandle::Type::Win32) {
        m_hwnd = reinterpret_cast<HWND>(ci.window.win32.hWnd);
        if (!createWGLContext(m_hwnd, ci.enableValidation)) {
            std::fprintf(stderr, "[GLDevice] Failed to create WGL context\n");
            return;
        }
    }
#endif

    // 加载 OpenGL 函数指针 (GLAD)
    // Emscripten: WebGL 函数已静态链接，无需 GLAD 动态加载
#ifndef __EMSCRIPTEN__
    if (!gladLoadGL()) {
        std::fprintf(stderr, "[GLDevice] Failed to load OpenGL via GLAD\n");
        shutdown();
        return;
    }
#endif

    std::fprintf(stdout, "[GLDevice] OpenGL %s | %s | %s\n",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

    // Debug output（OpenGL 4.3+，WebGL/Emscripten 不支持）
#if defined(_DEBUG) && !defined(__EMSCRIPTEN__)
    if (ci.enableValidation && glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
            [](GLenum source, GLenum type, GLuint id, GLenum severity,
               GLsizei /*length*/, const GLchar* message, const void* /*userParam*/) {
                if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
                const char* sevStr = "???";
                switch (severity) {
                case GL_DEBUG_SEVERITY_HIGH:         sevStr = "HIGH"; break;
                case GL_DEBUG_SEVERITY_MEDIUM:       sevStr = "MEDIUM"; break;
                case GL_DEBUG_SEVERITY_LOW:          sevStr = "LOW"; break;
                case GL_DEBUG_SEVERITY_NOTIFICATION: sevStr = "NOTIFY"; break;
                }
                std::fprintf(stderr, "[GL %s] %s\n", sevStr, message);
            },
            nullptr);
    }
#endif

    queryCapabilities();

    // 默认 GL 状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    m_frameCommandList = std::make_unique<GLCommandList>();
    m_initialized = true;
    std::fprintf(stdout, "[GLDevice] Initialization complete\n");
}

GLDevice::~GLDevice() {
    waitIdle();
    shutdown();
}

void GLDevice::shutdown() {
#ifdef _WIN32
    if (m_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
    }
#endif
    m_initialized = false;
}

void GLDevice::queryCapabilities() {
    m_caps.backend = GraphicsBackend::OpenGL;

    GLint val = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &val);
    m_caps.maxTextureSize = static_cast<uint32_t>(val);

    // GL 4.6 makes anisotropic filtering core; for 4.5, check extension
    if (isExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        glGetIntegerv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY*/, &val);
        m_caps.maxTextureAniso = static_cast<uint32_t>(val);
    }

    m_caps.depthClamp        = true;  // Core since GL 3.2
    m_caps.geometryShader    = true;  // Core since GL 3.2
    m_caps.tessellationShader = true;  // Core since GL 4.0
    m_caps.computeShader     = true;  // Core since GL 4.3
}

// ============================================================
// Win32 WGL 上下文创建
// ============================================================

#ifdef _WIN32

bool GLDevice::createWGLContext(HWND hwnd, bool enableValidation) {
    m_hdc = GetDC(hwnd);
    if (!m_hdc) return false;

    // 基础像素格式（用于创建临时上下文）
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = m_renderConfig.stencilBuffer ? static_cast<BYTE>(8) : static_cast<BYTE>(0);
    pfd.iLayerType   = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (!pixelFormat) return false;
    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) return false;

    // 临时上下文 → 加载 WGL 扩展
    HGLRC tempRC = wglCreateContext(m_hdc);
    if (!tempRC) return false;
    wglMakeCurrent(m_hdc, tempRC);

    // 手动加载 wglCreateContextAttribsARB
    auto wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 6,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            WGL_CONTEXT_FLAGS_ARB,
                (enableValidation ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
            0
        };

        m_hglrc = wglCreateContextAttribsARB(m_hdc, nullptr, attribs);
    }

    // 销毁临时上下文
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempRC);

    if (!m_hglrc) {
        std::fprintf(stderr, "[GLDevice] wglCreateContextAttribsARB failed, "
                             "falling back to legacy context\n");
        m_hglrc = wglCreateContext(m_hdc);
    }

    if (!m_hglrc) return false;

    wglMakeCurrent(m_hdc, m_hglrc);
    return true;
}

#endif // _WIN32

// ============================================================
// 资源创建（桩实现 — TODO: 后续补全具体 GL 子类）
// ============================================================

ResourcePtr<Buffer> GLDevice::createBuffer(const BufferDesc& desc) {
    auto* buffer = new GLBuffer(desc);
    if (buffer && buffer->isValid()) {
        return ResourcePtr<Buffer>(buffer, DeviceResourceDeleter{shared_from_this()});
    }
    delete buffer;
    std::fprintf(stderr, "[GLDevice] Failed to create buffer: %s\n",
                 std::string(desc.name).c_str());
    return nullptr;
}

ResourcePtr<Texture> GLDevice::createTexture(const TextureDesc& desc) {
    auto* texture = new GLTexture(desc);
    if (texture && texture->isValid()) {
        return ResourcePtr<Texture>(texture, DeviceResourceDeleter{shared_from_this()});
    }
    delete texture;
    std::fprintf(stderr, "[GLDevice] Failed to create texture: %s\n",
                 std::string(desc.name).c_str());
    return nullptr;
}

ResourcePtr<Shader> GLDevice::createShader(const ShaderDesc& desc) {
    auto* shader = new GLShader(desc);
    if (shader && shader->isValid()) {
        return ResourcePtr<Shader>(shader, DeviceResourceDeleter{shared_from_this()});
    }
    delete shader;
    std::fprintf(stderr, "[GLDevice] Failed to create shader: %s\n",
                 std::string(desc.name).c_str());
    return nullptr;
}

ResourcePtr<PipelineState> GLDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    auto* pipeline = new GLPipelineState(desc);
    if (pipeline && pipeline->isValid()) {
        return ResourcePtr<PipelineState>(pipeline, DeviceResourceDeleter{shared_from_this()});
    }
    delete pipeline;
    std::fprintf(stderr, "[GLDevice] Failed to create pipeline state: %s\n",
                 std::string(desc.name).c_str());
    return nullptr;
}

ResourcePtr<CommandList> GLDevice::createCommandList() {
    auto* cmdList = new GLCommandList();
    if (cmdList)
        return ResourcePtr<CommandList>(cmdList, DeviceResourceDeleter{shared_from_this()});
    delete cmdList;
    std::fprintf(stderr, "[GLDevice] Failed to create command list\n");
    return nullptr;
}

ResourcePtr<SwapChain> GLDevice::createSwapChain(const SwapChainDesc& desc) {
#ifdef _WIN32
    GLSwapChain::InitParams params;
    params.hdc  = m_hdc;
    params.hwnd = m_hwnd;
    return ResourcePtr<SwapChain>(new GLSwapChain(desc, params, m_renderConfig), DeviceResourceDeleter{shared_from_this()});
#else
    std::fprintf(stderr, "[GLDevice] createSwapChain: unsupported platform\n");
    return nullptr;
#endif
}

ResourcePtr<Fence> GLDevice::createFence(uint64_t /*initialValue*/) {
    // TODO: return new GLFence();
    std::fprintf(stderr, "[GLDevice] createFence: not yet implemented\n");
    return nullptr;
}

ResourcePtr<RenderTarget> GLDevice::createRenderTarget(const RenderTargetDesc& desc) {
    auto* rt = new GLRenderTarget(desc);
    if (rt && rt->isValid()) {
        return ResourcePtr<RenderTarget>(rt, DeviceResourceDeleter{shared_from_this()});
    }
    delete rt;
    std::fprintf(stderr, "[GLDevice] Failed to create render target (%ux%u)\n",
                 desc.width, desc.height);
    return nullptr;
}

ResourcePtr<Sampler> GLDevice::createSampler(const SamplerDesc& desc) {
    return ResourcePtr<Sampler>(new GLSampler(desc), DeviceResourceDeleter{shared_from_this()});
}

// ============================================================
// 资源销毁
// ============================================================

void GLDevice::destroy(Buffer* resource)        { delete resource; }
void GLDevice::destroy(Texture* resource)       { delete resource; }
void GLDevice::destroy(Shader* resource)        { delete resource; }
void GLDevice::destroy(PipelineState* resource) { delete resource; }
void GLDevice::destroy(CommandList* resource)   { delete resource; }
void GLDevice::destroy(SwapChain* resource)     { delete resource; }
void GLDevice::destroy(RenderTarget* resource)  { delete resource; }
void GLDevice::destroy(Sampler* resource)       { delete resource; }
void GLDevice::destroy(Fence* resource)         { delete resource; }

// ============================================================
// 命令提交
// ============================================================

void GLDevice::executeCommandLists(CommandList** /*cmdLists*/,
                                   uint32_t /*count*/,
                                   Fence* /*fence*/,
                                   uint64_t /*fenceValue*/) {
    // OpenGL 是立即模式，命令在录制时已执行
    // 只需 glFlush 确保提交
    glFlush();
}

void GLDevice::waitIdle() {
    if (m_initialized) {
        glFinish();
    }
}

// ============================================================
// 帧循环
// ============================================================

void GLDevice::beginFrame() {
    // OpenGL 无需 acquire image，直接开始新帧
    // clear 由 SwapChain/RenderTarget 的 beginRenderPass 负责
}

CommandList* GLDevice::frameCommandList() {
    // 设备未初始化，返回空指针由上层 if (!cmd) return 捕获
    if (!m_initialized) {
        return nullptr;
    }

    // m_frameCommandList 是直接成员，地址固定，无堆指针风险
    return m_frameCommandList.get();
}

void GLDevice::submitAndPresent(SwapChain* /*swapchain*/) {
    // OpenGL: 命令已执行，只需 swap buffers
#ifdef _WIN32
    if (m_hdc) {
        SwapBuffers(m_hdc);
    }
#endif
}

void GLDevice::submitOffscreen() {
    // 离屏渲染：无 present，仅 flush 确保命令提交
    glFlush();
}

} // namespace mulan::Engine
