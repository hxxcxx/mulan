#include "detail/gl_render_target.h"
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

// ============================================================
// 格式转换
// ============================================================

GLenum GLRenderTarget::toGLInternalFormat(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm: return GL_RGBA8;
    case TextureFormat::BGRA8_UNorm: return GL_RGBA8;  // GL 无 BGRA 内部格式，用 RGBA8
    case TextureFormat::R8_UNorm: return GL_R8;
    case TextureFormat::RGBA16_Float: return GL_RGBA16F;
    case TextureFormat::R16_Float: return GL_R16F;
    case TextureFormat::RG16_Float: return GL_RG16F;
    case TextureFormat::RGBA32_Float: return GL_RGBA32F;
    case TextureFormat::R32_Float: return GL_R32F;
    case TextureFormat::RG32_Float: return GL_RG32F;
    case TextureFormat::RGBA8_sRGB: return GL_SRGB8_ALPHA8;
    case TextureFormat::BGRA8_sRGB: return GL_SRGB8_ALPHA8;
    default: return 0;
    }
}

GLenum GLRenderTarget::toGLDepthAttachment(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::D16_UNorm: return GL_DEPTH_ATTACHMENT;
    case TextureFormat::D32_Float: return GL_DEPTH_ATTACHMENT;
    case TextureFormat::D24_UNorm_S8_UInt: return GL_DEPTH_STENCIL_ATTACHMENT;
    case TextureFormat::D32_Float_S8X24_UInt: return GL_DEPTH_STENCIL_ATTACHMENT;
    default: return GL_DEPTH_STENCIL_ATTACHMENT;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

GLRenderTarget::GLRenderTarget(const RenderTargetDesc& desc) : desc_(desc) {
    (void) createResources();
}

GLRenderTarget::~GLRenderTarget() {
    destroyResources();
}

// ============================================================
// 资源创建与销毁
// ============================================================

Result<void> GLRenderTarget::createResources() {
    const GLenum colorFormat = toGLInternalFormat(desc_.colorFormat);
    if (colorFormat == 0) {
        return std::unexpected(makeError(EngineErrorCode::RenderTargetCreateFailed, "OpenGL 不支持请求的颜色附件格式"));
    }
    // --- Color 纹理 ---
    GLuint colorTex = 0;
    const GLenum colorTarget = desc_.sampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    glCreateTextures(colorTarget, 1, &colorTex);
    if (!colorTex) {
        LOG_ERROR("[OpenGL] Render target creation failed: color texture creation failed");
        return std::unexpected(
                makeError(EngineErrorCode::RenderTargetCreateFailed, "OpenGL color texture creation failed"));
    }
    if (desc_.sampleCount > 1) {
        glTextureStorage2DMultisample(colorTex, static_cast<GLsizei>(desc_.sampleCount), colorFormat,
                                      static_cast<GLsizei>(desc_.width), static_cast<GLsizei>(desc_.height), GL_TRUE);
    } else {
        glTextureStorage2D(colorTex, 1, colorFormat, static_cast<GLsizei>(desc_.width),
                           static_cast<GLsizei>(desc_.height));
        glTextureParameteri(colorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(colorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(colorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(colorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    TextureDesc colorDesc = TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat, "GLOffscreenColor",
                                                      desc_.sampleCount);
    if (desc_.sampleCount > 1) {
        msaa_color_texture_ = std::make_unique<GLTexture>(colorDesc, colorTex);
        auto resolveDesc =
                TextureDesc::renderTarget(desc_.width, desc_.height, desc_.colorFormat, "GLOffscreenResolveColor", 1);
        color_texture_ = std::make_unique<GLTexture>(resolveDesc);
    } else {
        color_texture_ = std::make_unique<GLTexture>(colorDesc, colorTex);
    }

    // --- Depth 纹理 ---
    GLuint depthTex = 0;
    const GLenum depthTarget = desc_.sampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    if (desc_.hasDepth) {
        glCreateTextures(depthTarget, 1, &depthTex);
        if (!depthTex) {
            LOG_ERROR("[OpenGL] Render target creation failed: depth texture creation failed");
            destroyResources();
            return std::unexpected(
                    makeError(EngineErrorCode::RenderTargetCreateFailed, "OpenGL depth texture creation failed"));
        }

        // 根据深度格式选择 GL 内部格式
        GLenum internalFmt = GL_DEPTH24_STENCIL8;
        switch (desc_.depthFormat) {
        case TextureFormat::D16_UNorm: internalFmt = GL_DEPTH_COMPONENT16; break;
        case TextureFormat::D32_Float: internalFmt = GL_DEPTH_COMPONENT32F; break;
        case TextureFormat::D32_Float_S8X24_UInt: internalFmt = GL_DEPTH32F_STENCIL8; break;
        default:  // D24_UNorm_S8_UInt
            break;
        }

        if (desc_.sampleCount > 1) {
            glTextureStorage2DMultisample(depthTex, static_cast<GLsizei>(desc_.sampleCount), internalFmt,
                                          static_cast<GLsizei>(desc_.width), static_cast<GLsizei>(desc_.height),
                                          GL_TRUE);
        } else {
            glTextureStorage2D(depthTex, 1, internalFmt, static_cast<GLsizei>(desc_.width),
                               static_cast<GLsizei>(desc_.height));
            glTextureParameteri(depthTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(depthTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(depthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(depthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        TextureDesc depthDesc = TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat,
                                                          "GLOffscreenDepth", desc_.sampleCount);
        depth_texture_ = std::make_unique<GLTexture>(depthDesc, depthTex);
    }

    // --- FBO ---
    glCreateFramebuffers(1, &fbo_);
    if (!fbo_) {
        LOG_ERROR("[OpenGL] Render target creation failed: glCreateFramebuffers returned 0");
        return std::unexpected(
                makeError(EngineErrorCode::RenderTargetCreateFailed, "OpenGL framebuffer creation failed"));
    }

    // Attach color
    glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, colorTex, 0);

    // Attach depth
    if (desc_.hasDepth && depthTex) {
        GLenum depthAttach = toGLDepthAttachment(desc_.depthFormat);
        glNamedFramebufferTexture(fbo_, depthAttach, depthTex, 0);
    }

    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(fbo_, 1, &drawBuffer);
    glNamedFramebufferReadBuffer(fbo_, GL_COLOR_ATTACHMENT0);

    // 完整性检查
    GLenum status = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("[OpenGL] Render target creation failed: framebuffer status=0x{:X}", status);
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
        return std::unexpected(
                makeError(EngineErrorCode::RenderTargetCreateFailed, "OpenGL framebuffer is incomplete"));
    }

    GLTexture* renderColor = msaa_color_texture_ ? msaa_color_texture_.get() : color_texture_.get();
    if (renderColor)
        renderColor->setRenderTargetFramebuffer(fbo_);
    if (depth_texture_)
        depth_texture_->setRenderTargetFramebuffer(fbo_);

    LOG_DEBUG("[OpenGL] Framebuffer created: handle={}, size={}x{}, color={}, depth={}", fbo_, desc_.width,
              desc_.height, colorTex, depthTex);
    return {};
}

void GLRenderTarget::destroyResources() {
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    color_texture_.reset();
    msaa_color_texture_.reset();
    depth_texture_.reset();
}

RenderPassBeginInfo GLRenderTarget::renderPassBeginInfo() {
    auto info = RenderTarget::renderPassBeginInfo();
    if (msaa_color_texture_) {
        info.colorAttachments[0].target = msaa_color_texture_.get();
        info.colorAttachments[0].resolveTarget = color_texture_.get();
    }
    return info;
}

// ============================================================
// resize
// ============================================================

Result<void> GLRenderTarget::resize(uint32_t width, uint32_t height) {
    if (desc_.width == width && desc_.height == height)
        return {};
    if (width == 0 || height == 0)
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "OpenGL render target size must be non-zero"));
    desc_.width = width;
    desc_.height = height;
    destroyResources();
    if (auto result = createResources(); !result)
        return std::unexpected(result.error());
    return {};
}

// ============================================================
}  // namespace mulan::engine
