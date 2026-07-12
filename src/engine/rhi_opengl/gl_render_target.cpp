#include "detail/gl_render_target.h"

#include <cstdio>

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
    default: return GL_RGBA8;
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
    createResources();
}

GLRenderTarget::~GLRenderTarget() {
    destroyResources();
}

// ============================================================
// 资源创建与销毁
// ============================================================

void GLRenderTarget::createResources() {
    // --- Color 纹理 ---
    GLuint colorTex = 0;
    glGenTextures(1, &colorTex);
    const GLenum colorTarget = desc_.sampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    glBindTexture(colorTarget, colorTex);
    if (desc_.sampleCount > 1) {
        glTexStorage2DMultisample(colorTarget, static_cast<GLsizei>(desc_.sampleCount),
                                  toGLInternalFormat(desc_.colorFormat), static_cast<GLsizei>(desc_.width),
                                  static_cast<GLsizei>(desc_.height), GL_TRUE);
    } else {
        glTexImage2D(colorTarget, 0, toGLInternalFormat(desc_.colorFormat), static_cast<GLsizei>(desc_.width),
                     static_cast<GLsizei>(desc_.height), 0, GLTexture::toGLBaseFormat(desc_.colorFormat),
                     GLTexture::toGLType(desc_.colorFormat), nullptr);
        glTexParameteri(colorTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(colorTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(colorTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(colorTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(colorTarget, 0);

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
        glGenTextures(1, &depthTex);
        glBindTexture(depthTarget, depthTex);

        // 根据深度格式选择 GL 内部格式
        GLenum internalFmt = GL_DEPTH24_STENCIL8;
        GLenum pixelFmt = GL_DEPTH_STENCIL;
        GLenum pixelType = GL_UNSIGNED_INT_24_8;
        switch (desc_.depthFormat) {
        case TextureFormat::D16_UNorm:
            internalFmt = GL_DEPTH_COMPONENT16;
            pixelFmt = GL_DEPTH_COMPONENT;
            pixelType = GL_UNSIGNED_SHORT;
            break;
        case TextureFormat::D32_Float:
            internalFmt = GL_DEPTH_COMPONENT32F;
            pixelFmt = GL_DEPTH_COMPONENT;
            pixelType = GL_FLOAT;
            break;
        case TextureFormat::D32_Float_S8X24_UInt:
            internalFmt = GL_DEPTH32F_STENCIL8;
            pixelFmt = GL_DEPTH_STENCIL;
            pixelType = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
            break;
        default:  // D24_UNorm_S8_UInt
            break;
        }

        if (desc_.sampleCount > 1) {
            glTexStorage2DMultisample(depthTarget, static_cast<GLsizei>(desc_.sampleCount), internalFmt,
                                      static_cast<GLsizei>(desc_.width), static_cast<GLsizei>(desc_.height), GL_TRUE);
        } else {
            glTexImage2D(depthTarget, 0, internalFmt, static_cast<GLsizei>(desc_.width),
                         static_cast<GLsizei>(desc_.height), 0, pixelFmt, pixelType, nullptr);
            glTexParameteri(depthTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(depthTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(depthTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(depthTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(depthTarget, 0);

        TextureDesc depthDesc = TextureDesc::depthStencil(desc_.width, desc_.height, desc_.depthFormat,
                                                          "GLOffscreenDepth", desc_.sampleCount);
        depth_texture_ = std::make_unique<GLTexture>(depthDesc, depthTex);
    }

    // --- FBO ---
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Attach color
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTarget, colorTex, 0);

    // Attach depth
    if (desc_.hasDepth && depthTex) {
        GLenum depthAttach = toGLDepthAttachment(desc_.depthFormat);
        glFramebufferTexture2D(GL_FRAMEBUFFER, depthAttach, depthTarget, depthTex, 0);
    }

    // 完整性检查
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[GLRenderTarget] Framebuffer incomplete (status: 0x%X)\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::fprintf(stdout, "[GLRenderTarget] Created FBO %u (%ux%u color=%u depth=%u)\n", fbo_, desc_.width, desc_.height,
                 colorTex, depthTex);
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

void GLRenderTarget::resize(uint32_t width, uint32_t height) {
    if (desc_.width == width && desc_.height == height)
        return;
    desc_.width = width;
    desc_.height = height;
    destroyResources();
    createResources();
}

// ============================================================
}  // namespace mulan::engine
