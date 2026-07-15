#include "detail/gl_device.h"
#include "../rhi/pipeline_validation.h"
#include "detail/gl_swap_chain.h"
#include "detail/gl_shader.h"
#include "detail/gl_pipeline_state.h"
#include "detail/gl_buffer.h"
#include "detail/gl_bind_group.h"
#include "detail/gl_fence.h"
#include "detail/gl_command_list.h"
#include "detail/gl_render_target.h"
#include "detail/gl_texture.h"
#include "../rhi/engine_error_code.h"

#include <cstring>
#include <algorithm>
#include <expected>
#include <string>
#include <utility>

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

void APIENTRY glDebugMessageCallbackProc(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                         const GLchar* message, const void* /*userParam*/) noexcept {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;

    const char* severity_name = "UNKNOWN";
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH: severity_name = "HIGH"; break;
    case GL_DEBUG_SEVERITY_MEDIUM: severity_name = "MEDIUM"; break;
    case GL_DEBUG_SEVERITY_LOW: severity_name = "LOW"; break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: severity_name = "NOTIFICATION"; break;
    }

    const std::string text =
            !message ? "<null debug message>"
                     : (length >= 0 ? std::string(message, static_cast<size_t>(length)) : std::string(message));
    if (severity == GL_DEBUG_SEVERITY_HIGH)
        LOG_ERROR("[OpenGL Debug] severity={} source=0x{:X} type=0x{:X} id={} {}", severity_name, source, type, id,
                  text);
    else
        LOG_WARN("[OpenGL Debug] severity={} source=0x{:X} type=0x{:X} id={} {}", severity_name, source, type, id,
                 text);
}
}  // anonymous namespace

// ============================================================
// 初始化 / 销毁
// ============================================================

void GLDevice::init(const CreateInfo& ci) {
    native_window_ = ci.window;
    render_config_ = ci.renderConfig;

    GLContextCreateInfo context_info;
    context_info.window = ci.window;
    context_info.renderConfig = ci.renderConfig;
    context_info.enableValidation = ci.enableValidation;
    auto context_result = createGLContext(context_info);
    if (!context_result) {
        LOG_ERROR("[OpenGL] Context creation failed: {}", context_result.error().message);
        return;
    }
    context_ = std::move(*context_result);

    // 加载 OpenGL 函数指针 (GLAD)
    // Load native OpenGL entry points.
    if (!gladLoadGL()) {
        LOG_ERROR("[OpenGL] GLAD initialization failed");
        shutdown();
        return;
    }

    GLint major_version = 0;
    GLint minor_version = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major_version);
    glGetIntegerv(GL_MINOR_VERSION, &minor_version);
    if (major_version < 4 || (major_version == 4 && minor_version < 6)) {
        LOG_ERROR("[OpenGL] OpenGL 4.6 Core Profile is required; detected {}.{}", major_version, minor_version);
        shutdown();
        return;
    }

    GLint profile_mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    if ((profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        LOG_ERROR("[OpenGL] Core Profile is required; profileMask=0x{:X}", profile_mask);
        shutdown();
        return;
    }

    LOG_INFO("[OpenGL] Runtime initialized: version={}, renderer={}, vendor={}",
             reinterpret_cast<const char*>(glGetString(GL_VERSION)),
             reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
             reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

    // Debug output（OpenGL 4.3+）
#ifdef _DEBUG
    if (ci.enableValidation && glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&glDebugMessageCallbackProc, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW, 0, nullptr, GL_FALSE);
    }
#endif

    queryCapabilities();

    // 默认 GL 状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    frame_command_list_ =
            std::make_unique<GLCommandList>(caps_.minUniformBufferOffsetAlignment, caps_.maxUniformBufferBindingSize);
    frame_command_list_->trackResource(*this, RHIResourceKind::CommandList, "OpenGLFrameCommandList");
    auto submissionFenceResult = createFence(0);
    if (!submissionFenceResult) {
        LOG_ERROR("[OpenGL] Submission timeline creation failed: {}", submissionFenceResult.error().message);
        frame_command_list_.reset();
        shutdown();
        return;
    }
    initializeSubmissionTracking(std::move(*submissionFenceResult));
    initialized_ = true;
    LOG_INFO("[OpenGL] Device initialization complete");
}

GLDevice::~GLDevice() {
    if (initialized_) {
        if (auto result = waitIdle(); !result)
            LOG_ERROR("[OpenGL] Device idle wait during shutdown failed: {}", result.error().message);
    }
    drainDeferredReleases();
    shutdownSubmissionTracking();
    frame_command_list_.reset();
    shutdown();
}

void GLDevice::shutdown() {
    context_.reset();
    initialized_ = false;
}

void GLDevice::queryCapabilities() {
    caps_.backend = GraphicsBackend::OpenGL;

    GLint val = 0;
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &val);
    caps_.maxTextureSize = static_cast<uint32_t>(val);

    glGetIntegerv(GL_MAX_SAMPLES, &val);
    caps_.maxSampleCount = static_cast<uint32_t>(std::max(1, val));
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &val);
    caps_.minUniformBufferOffsetAlignment = static_cast<uint32_t>(std::max(1, val));
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &val);
    caps_.maxUniformBufferBindingSize = static_cast<uint32_t>(std::max(1, val));

    // GL 4.6 makes anisotropic filtering core; for 4.5, check extension
    if (isExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        glGetIntegerv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY*/, &val);
        caps_.maxTextureAniso = static_cast<uint32_t>(val);
    }

    const int version = major * 10 + minor;
    caps_.geometryShader = version >= 32;
    caps_.computeShader = false;
    caps_.indirectDraw = false;
    caps_.indirectDispatch = false;
    caps_.pushConstants = false;
}

// ============================================================
// 资源创建
// ============================================================

template <typename Base, typename Impl, typename Desc>
static core::Result<std::unique_ptr<Base>> createGLResource(RHIDevice& device, const Desc& desc, EngineErrorCode code,
                                                            RHIResourceKind kind, const char* label) {
    try {
        auto resource = std::make_unique<Impl>(desc);
        if (!resource->isValid())
            return std::unexpected(makeError(code, label));
        if constexpr (requires { desc.name; })
            resource->trackResource(device, kind, desc.name);
        else
            resource->trackResource(device, kind, label);
        return std::unique_ptr<Base>(std::move(resource));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(code, e.what()));
    }
}

core::Result<std::unique_ptr<Buffer>> GLDevice::createBuffer(const BufferDesc& desc) {
    return createGLResource<Buffer, GLBuffer>(*this, desc, EngineErrorCode::BufferCreateFailed, RHIResourceKind::Buffer,
                                              "OpenGL buffer creation failed");
}

core::Result<std::unique_ptr<Texture>> GLDevice::createTexture(const TextureDesc& desc) {
    return createGLResource<Texture, GLTexture>(*this, desc, EngineErrorCode::TextureCreateFailed,
                                                RHIResourceKind::Texture, "OpenGL texture creation failed");
}

core::Result<std::unique_ptr<Shader>> GLDevice::createShader(const ShaderDesc& desc) {
    return createGLResource<Shader, GLShader>(*this, desc, EngineErrorCode::ShaderCompileFailed,
                                              RHIResourceKind::Shader, "OpenGL shader creation failed");
}

core::Result<std::unique_ptr<PipelineState>> GLDevice::createPipelineState(const GraphicsPipelineDesc& desc) {
    if (auto validation = validateGraphicsPipelineDesc(desc, *this, caps_); !validation)
        return std::unexpected(validation.error());
    return createGLResource<PipelineState, GLPipelineState>(*this, desc, EngineErrorCode::PipelineCreateFailed,
                                                            RHIResourceKind::PipelineState,
                                                            "OpenGL pipeline creation failed");
}

core::Result<std::unique_ptr<ComputePipelineState>> GLDevice::createComputePipelineState(const ComputePipelineDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "OpenGL compute pipeline is not implemented"));
}

core::Result<std::unique_ptr<CommandList>> GLDevice::createCommandList() {
    try {
        auto command_list = std::make_unique<GLCommandList>(caps_.minUniformBufferOffsetAlignment,
                                                            caps_.maxUniformBufferBindingSize);
        command_list->trackResource(*this, RHIResourceKind::CommandList, "OpenGLCommandList");
        return std::unique_ptr<CommandList>(std::move(command_list));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed, e.what()));
    }
}

core::Result<std::unique_ptr<SwapChain>> GLDevice::createSwapChain(const SwapChainDesc& desc) {
    if (!context_)
        return std::unexpected(makeError(EngineErrorCode::SwapChainCreateFailed, "OpenGL context is not initialized"));
    auto swap_chain = std::make_unique<GLSwapChain>(desc, *context_, render_config_);
    swap_chain->trackResource(*this, RHIResourceKind::SwapChain, "OpenGLSwapChain");
    return std::unique_ptr<SwapChain>(std::move(swap_chain));
}

core::Result<std::unique_ptr<Fence>> GLDevice::createFence(uint64_t initialValue) {
    try {
        auto fence = std::make_unique<GLFence>(initialValue);
        fence->trackResource(*this, RHIResourceKind::Fence, "OpenGLFence");
        return std::unique_ptr<Fence>(std::move(fence));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::FenceCreateFailed, e.what()));
    }
}

core::Result<std::unique_ptr<RenderTarget>> GLDevice::createRenderTarget(const RenderTargetDesc& desc) {
    if (auto validation = validateRenderTargetDesc(desc, caps_); !validation)
        return std::unexpected(validation.error());
    return createGLResource<RenderTarget, GLRenderTarget>(*this, desc, EngineErrorCode::RenderTargetCreateFailed,
                                                          RHIResourceKind::RenderTarget,
                                                          "OpenGL render target creation failed");
}

core::Result<std::unique_ptr<Sampler>> GLDevice::createSampler(const SamplerDesc& desc) {
    try {
        auto sampler = std::make_unique<GLSampler>(desc);
        if (!sampler->isValid())
            return std::unexpected(makeError(EngineErrorCode::SamplerCreateFailed, "OpenGL sampler creation failed"));
        sampler->trackResource(*this, RHIResourceKind::Sampler, "OpenGLSampler");
        return std::unique_ptr<Sampler>(std::move(sampler));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::SamplerCreateFailed, e.what()));
    }
}

core::Result<std::unique_ptr<BindGroup>> GLDevice::createBindGroup(const BindGroupLayout& layout,
                                                                   const BindGroupDesc& desc) {
    const std::string validationError = validateBindGroupDesc(
            layout, desc, { caps_.minUniformBufferOffsetAlignment, caps_.maxUniformBufferBindingSize });
    if (!validationError.empty())
        return std::unexpected(makeError(EngineErrorCode::ResourceCreateFailed, validationError));
    try {
        auto bind_group = std::make_unique<GLBindGroup>(
                layout, desc,
                BindGroupValidationLimits{ caps_.minUniformBufferOffsetAlignment, caps_.maxUniformBufferBindingSize });
        bind_group->trackResource(*this, RHIResourceKind::BindGroup, "OpenGLBindGroup");
        return std::unique_ptr<BindGroup>(std::move(bind_group));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::ResourceCreateFailed, e.what()));
    }
}

core::Result<void> GLDevice::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    assertResourceOwned(dst);
    const uint32_t bpp = textureFormatBytesPerPixel(upload.format);
    const uint32_t rowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bpp;
    auto* texture = static_cast<GLTexture*>(dst);
    if (!texture || upload.data.empty() || bpp == 0 || rowPitch % bpp != 0 ||
        upload.data.size_bytes() < static_cast<size_t>(rowPitch) * upload.height)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "OpenGL texture upload arguments are invalid"));

    const auto& destinationDesc = texture->desc();
    if (destinationDesc.dimension != TextureDimension::Texture2D || destinationDesc.sampleCount != 1 ||
        upload.mipLevel >= destinationDesc.mipLevels || upload.arrayLayer >= destinationDesc.arraySize ||
        upload.format != destinationDesc.format || upload.depth != 1 ||
        upload.width != std::max(1u, destinationDesc.width >> upload.mipLevel) ||
        upload.height != std::max(1u, destinationDesc.height >> upload.mipLevel))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "OpenGL texture upload subresource is invalid"));

    GLint previous_alignment = 4;
    GLint previous_row_length = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(rowPitch / bpp));
    texture->upload(upload.mipLevel, upload.data.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "OpenGL texture upload failed"));
    return {};
}

core::Result<SubmissionToken> GLDevice::executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence,
                                                            uint64_t fenceValue) {
    if (auto validation = validateCommandListsForSubmission(cmdLists, count); !validation)
        return std::unexpected(validation.error());
    if (!cmdLists || count == 0)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "OpenGL command list batch is empty"));
    for (uint32_t i = 0; i < count; ++i)
        assertResourceOwned(cmdLists[i]);
    if (fence)
        assertResourceOwned(fence);
    auto submissionLock = lockSubmissionQueue();
    if (fence) {
        if (auto signal = fence->signal(fenceValue); !signal)
            return std::unexpected(signal.error());
    }

    const SubmissionToken token = reserveSubmissionToken();
    auto* completionFence = static_cast<GLFence*>(submissionFence());
    if (!token || !completionFence)
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "OpenGL submission timeline is unavailable"));
    if (auto signal = completionFence->signal(token.value); !signal)
        return std::unexpected(signal.error());
    glFlush();
    if (!completionFence->isValid()) {
        LOG_ERROR("[OpenGL] Standalone submission timeline signal failed");
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "OpenGL submission timeline signal failed"));
    }
    for (uint32_t i = 0; i < count; ++i)
        cmdLists[i]->markSubmitted(token);
    commitSubmission(token);
    return token;
}

core::Result<void> GLDevice::waitIdle() {
    if (!initialized_)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL device is unavailable"));
    glFinish();
    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        return std::unexpected(makeError(EngineErrorCode::SubmissionWaitFailed, "OpenGL glFinish failed"));
    collectGarbage();
    return {};
}

core::Result<CommandList*> GLDevice::beginFrame(SwapChain* swapchain) {
    if (swapchain)
        assertResourceOwned(swapchain);
    if (context_ && !context_->makeCurrent()) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL context activation failed"));
    }
    collectGarbage();
    if (!initialized_ || !frame_command_list_)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "OpenGL frame CommandList is unavailable"));
    if (auto result = frame_command_list_->begin(); !result)
        return std::unexpected(result.error());
    return frame_command_list_.get();
}

core::Result<SubmissionToken> GLDevice::submitFrame() {
    auto submissionLock = lockSubmissionQueue();
    const SubmissionToken token = reserveSubmissionToken();
    if (!token)
        return std::unexpected(
                makeError(EngineErrorCode::SubmissionFailed, "OpenGL submission timeline is unavailable"));
    auto* completionFence = static_cast<GLFence*>(submissionFence());
    if (auto signal = completionFence->signal(token.value); !signal)
        return std::unexpected(signal.error());
    glFlush();
    if (!completionFence->isValid())
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "glFenceSync failed"));
    frame_command_list_->markSubmitted(token);
    commitSubmission(token);
    return token;
}

core::Result<SubmissionToken> GLDevice::endFrame(SwapChain* swapchain) {
    if (swapchain)
        assertResourceOwned(swapchain);
    if (auto recording = frame_command_list_->end(); !recording)
        return std::unexpected(recording.error());
    auto result = submitFrame();
    if (!result)
        return std::unexpected(result.error());
    if (swapchain) {
        if (auto presentResult = swapchain->present(); !presentResult)
            return std::unexpected(presentResult.error());
    }
    return result;
}

}  // namespace mulan::engine
