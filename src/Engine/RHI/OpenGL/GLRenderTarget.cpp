/**
 * @file GLRenderTarget.cpp
 * @brief OpenGL 离屏渲染目标实现（FBO）
 * @author hxxcxx
 * @date 2026-04-22
 */

#include "GLRenderTarget.h"

#include <cstdio>

namespace MulanGeo::engine {

// ============================================================
// 格式转换
// ============================================================

GLenum GLRenderTarget::toGLInternalFormat(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm:  return GL_RGBA8;
    case TextureFormat::BGRA8_UNorm:  return GL_RGBA8;   // GL 无 BGRA 内部格式，用 RGBA8
    case TextureFormat::RGBA16_Float: return GL_RGBA16F;
    case TextureFormat::RGBA32_Float: return GL_RGBA32F;
    default:                          return GL_RGBA8;
    }
}

GLenum GLRenderTarget::toGLDepthAttachment(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::D16_UNorm:              return GL_DEPTH_ATTACHMENT;
    case TextureFormat::D32_Float:              return GL_DEPTH_ATTACHMENT;
    case TextureFormat::D24_UNorm_S8_UInt:      return GL_DEPTH_STENCIL_ATTACHMENT;
    case TextureFormat::D32_Float_S8X24_UInt:   return GL_DEPTH_STENCIL_ATTACHMENT;
    default:                                    return GL_DEPTH_STENCIL_ATTACHMENT;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

GLRenderTarget::GLRenderTarget(const RenderTargetDesc& desc)
    : m_desc(desc)
{
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
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 toGLInternalFormat(m_desc.colorFormat),
                 static_cast<GLsizei>(m_desc.width),
                 static_cast<GLsizei>(m_desc.height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    TextureDesc colorDesc = TextureDesc::renderTarget(
        m_desc.width, m_desc.height, m_desc.colorFormat, "GLOffscreenColor");
    m_colorTexture = std::make_unique<GLTexture>(colorDesc, colorTex);

    // --- Depth 纹理 ---
    GLuint depthTex = 0;
    if (m_desc.hasDepth) {
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);

        // 根据深度格式选择 GL 内部格式
        GLenum internalFmt = GL_DEPTH24_STENCIL8;
        GLenum pixelFmt    = GL_DEPTH_STENCIL;
        GLenum pixelType   = GL_UNSIGNED_INT_24_8;
        switch (m_desc.depthFormat) {
        case TextureFormat::D16_UNorm:
            internalFmt = GL_DEPTH_COMPONENT16;
            pixelFmt    = GL_DEPTH_COMPONENT;
            pixelType   = GL_UNSIGNED_SHORT;
            break;
        case TextureFormat::D32_Float:
            internalFmt = GL_DEPTH_COMPONENT32F;
            pixelFmt    = GL_DEPTH_COMPONENT;
            pixelType   = GL_FLOAT;
            break;
        case TextureFormat::D32_Float_S8X24_UInt:
            internalFmt = GL_DEPTH32F_STENCIL8;
            pixelFmt    = GL_DEPTH_STENCIL;
            pixelType   = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
            break;
        default: // D24_UNorm_S8_UInt
            break;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt,
                     static_cast<GLsizei>(m_desc.width),
                     static_cast<GLsizei>(m_desc.height),
                     0, pixelFmt, pixelType, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        TextureDesc depthDesc = TextureDesc::depthStencil(
            m_desc.width, m_desc.height, m_desc.depthFormat, "GLOffscreenDepth");
        m_depthTexture = std::make_unique<GLTexture>(depthDesc, depthTex);
    }

    // --- FBO ---
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Attach color
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex, 0);

    // Attach depth
    if (m_desc.hasDepth && depthTex) {
        GLenum depthAttach = toGLDepthAttachment(m_desc.depthFormat);
        glFramebufferTexture2D(GL_FRAMEBUFFER, depthAttach,
                               GL_TEXTURE_2D, depthTex, 0);
    }

    // 完整性检查
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr,
            "[GLRenderTarget] Framebuffer incomplete (status: 0x%X)\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::fprintf(stdout,
        "[GLRenderTarget] Created FBO %u (%ux%u color=%u depth=%u)\n",
        m_fbo, m_desc.width, m_desc.height, colorTex, depthTex);
}

void GLRenderTarget::destroyResources() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    m_colorTexture.reset();
    m_depthTexture.reset();
}

// ============================================================
// resize
// ============================================================

void GLRenderTarget::resize(uint32_t width, uint32_t height) {
    if (m_desc.width == width && m_desc.height == height) return;
    m_desc.width  = width;
    m_desc.height = height;
    destroyResources();
    createResources();
}

// ============================================================
} // namespace MulanGeo::Engine
