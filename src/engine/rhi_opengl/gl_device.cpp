#include "detail/gl_device.h"
#include "detail/gl_swap_chain.h"
#include "detail/gl_shader.h"
#include "detail/gl_pipeline_state.h"
#include "detail/gl_buffer.h"
#include "detail/gl_command_list.h"
#include "detail/gl_render_target.h"
#include "detail/gl_texture.h"
#include "../rhi/engine_error_code.h"

#include <cstdio>
#include <cstring>
#include <expected>
#include <string>

namespace mulan::engine {

namespace {
bool isExtensionSupported(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && std::strcmp(ext, name) == 0)
            return true;
    }
    return false;
}
}  // anonymous namespace

// ============================================================
// 初始化 / 销毁
// ============================================================

void GLDevice::init(const CreateInfo& ci) {
    m_nativeWindow = ci.window;
    m_renderConfig = ci.renderConfig;

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
    // Load native OpenGL entry points.
    if (!gladLoadGL()) {
        std::fprintf(stderr, "[GLDevice] Failed to load OpenGL via GLAD\n");
        shutdown();
        return;
    }

    std::fprintf(stdout, "[GLDevice] OpenGL %s | %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

    // Debug output（OpenGL 4.3+，native OpenGL 不支持）
#ifdef _DEBUG
    if (ci.enableValidation && glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
                [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/, const GLchar* message,
                   const void* /*userParam*/) {
                    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
                        return;
                    const char* sevStr = "???";
                    switch (severity) {
                    case GL_DEBUG_SEVERITY_HIGH: sevStr = "HIGH"; break;
                    case GL_DEBUG_SEVERITY_MEDIUM: sevStr = "MEDIUM"; break;
                    case GL_DEBUG_SEVERITY_LOW: sevStr = "LOW"; break;
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

    m_caps.depthClamp = true;          // Core since GL 3.2
    m_caps.geometryShader = true;      // Core since GL 3.2
    m_caps.tessellationShader = true;  // Core since GL 4.0
    m_caps.computeShader = true;       // Core since GL 4.3
}

// ============================================================
// Win32 WGL 上下文创建
// ============================================================

#ifdef _WIN32

bool GLDevice::createWGLContext(HWND hwnd, bool enableValidation) {
    m_hdc = GetDC(hwnd);
    if (!m_hdc)
        return false;

    // 基础像素格式（用于创建临时上下文）
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = m_renderConfig.stencilBuffer ? static_cast<BYTE>(8) : static_cast<BYTE>(0);
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (!pixelFormat)
        return false;
    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd))
        return false;

    // 临时上下文 → 加载 WGL 扩展
    HGLRC tempRC = wglCreateContext(m_hdc);
    if (!tempRC)
        return false;
    wglMakeCurrent(m_hdc, tempRC);

    // 手动加载 wglCreateContextAttribsARB
    auto wglCreateContextAttribsARB =
            reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));

    if (wglCreateContextAttribsARB) {
        int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB,
                          4,
                          WGL_CONTEXT_MINOR_VERSION_ARB,
                          6,
                          WGL_CONTEXT_PROFILE_MASK_ARB,
                          WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                          WGL_CONTEXT_FLAGS_ARB,
                          (enableValidation ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
                          0 };

        m_hglrc = wglCreateContextAttribsARB(m_hdc, nullptr, attribs);
    }

    // 销毁临时上下文
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempRC);

    if (!m_hglrc) {
        std::fprintf(stderr,
                     "[GLDevice] wglCreateContextAttribsARB failed, "
                     "falling back to legacy context\n");
        m_hglrc = wglCreateContext(m_hdc);
    }

    if (!m_hglrc)
        return false;

    wglMakeCurrent(m_hdc, m_hglrc);
    return true;
}

#endif  // _WIN32

// ============================================================
// 资源创建
// ============================================================

template <typename Base, typename Impl, typename Desc>
static core::Result<std::unique_ptr<Base>> createGLResource(const Desc& desc, EngineErrorCode code, const char* label) {
    try {
        auto resource = std::make_unique<Impl>(desc);
        if (!resource->isValid())
            return std::unexpected(makeError(code, label));
        return std::unique_ptr<Base>(std::move(resource));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(code, e.what()));
    }
}

core::Result<std::unique_ptr<Buffer>> GLDevice::createBuffer(const BufferDesc& desc) {
    return createGLResource<Buffer, GLBuffer>(desc, EngineErrorCode::BufferCreateFailed,
                                              "OpenGL buffer creation failed");
}

core::Result<std::unique_ptr<Texture>> GLDevice::createTexture(const TextureDesc& desc) {
    return createGLResource<Texture, GLTexture>(desc, EngineErrorCode::TextureCreateFailed,
                                                "OpenGL texture creation failed");
}

core::Result<std::unique_ptr<Shader>> GLDevice::createShader(const ShaderDesc& desc) {
    return createGLResource<Shader, GLShader>(desc, EngineErrorCode::ShaderCompileFailed,
                                              "OpenGL shader creation failed");
}

core::Result<std::unique_ptr<PipelineState>> GLDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    return createGLResource<PipelineState, GLPipelineState>(desc, EngineErrorCode::PipelineCreateFailed,
                                                            "OpenGL pipeline creation failed");
}

core::Result<std::unique_ptr<ComputePipelineState>> GLDevice::createComputePipelineState(const ComputePipelineDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "OpenGL compute pipeline is not implemented"));
}

core::Result<std::unique_ptr<CommandList>> GLDevice::createCommandList() {
    try {
        return std::unique_ptr<CommandList>(std::make_unique<GLCommandList>());
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed, e.what()));
    }
}

core::Result<std::unique_ptr<SwapChain>> GLDevice::createSwapChain(const SwapChainDesc& desc) {
    GLSwapChain::InitParams params;
    params.hdc = m_hdc;
    params.hwnd = m_hwnd;
    return std::unique_ptr<SwapChain>(std::make_unique<GLSwapChain>(desc, params, m_renderConfig));
}

core::Result<std::unique_ptr<Fence>> GLDevice::createFence(uint64_t) {
    return std::unexpected(makeError(EngineErrorCode::BackendNotSupported, "OpenGL fence is not implemented"));
}

core::Result<std::unique_ptr<RenderTarget>> GLDevice::createRenderTarget(const RenderTargetDesc& desc) {
    return createGLResource<RenderTarget, GLRenderTarget>(desc, EngineErrorCode::RenderTargetCreateFailed,
                                                          "OpenGL render target creation failed");
}

core::Result<std::unique_ptr<Sampler>> GLDevice::createSampler(const SamplerDesc& desc) {
    try {
        return std::unique_ptr<Sampler>(std::make_unique<GLSampler>(desc));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::SamplerCreateFailed, e.what()));
    }
}

core::Result<std::unique_ptr<BindGroup>> GLDevice::createBindGroup(const BindGroupLayout&, const BindGroupDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "OpenGL bind-group objects are not implemented"));
}

void GLDevice::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    const uint32_t bpp = textureFormatBytesPerPixel(upload.format);
    const uint32_t rowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bpp;
    if (auto* texture = dynamic_cast<GLTexture*>(dst);
        texture && !upload.data.empty() && bpp && rowPitch % bpp == 0 &&
        upload.data.size_bytes() >= static_cast<size_t>(rowPitch) * upload.height) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(rowPitch / bpp));
        texture->upload(upload.mipLevel, upload.data.data());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
}

void GLDevice::executeCommandLists(CommandList**, uint32_t, Fence*, uint64_t) {
    glFlush();
}

void GLDevice::waitIdle() {
    if (m_initialized)
        glFinish();
}

void GLDevice::beginFrame(SwapChain*) {
}

CommandList* GLDevice::frameCommandList() {
    return m_initialized ? m_frameCommandList.get() : nullptr;
}

void GLDevice::submit() {
    glFlush();
}

void GLDevice::present(SwapChain* swapchain) {
    if (swapchain)
        swapchain->present();
}

void GLDevice::submitAndPresent(SwapChain* swapchain) {
    submit();
    present(swapchain);
}

void GLDevice::submitOffscreen() {
    submit();
}

}  // namespace mulan::engine
