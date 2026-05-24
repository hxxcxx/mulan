/**
 * @file GLSampler.cpp
 * @brief OpenGL 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#include "GLSampler.h"
#include <cstdio>

namespace MulanGeo::engine {

// ============================================================
// Helper: RHI enum → OpenGL enum
// ============================================================

static GLint toGLFilter(SamplerFilter min, SamplerFilter mag, SamplerFilter mip) {
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Nearest) {
        return (mip == SamplerFilter::Nearest) ? GL_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
    }
    if (min == SamplerFilter::Linear && mag == SamplerFilter::Linear) {
        return (mip == SamplerFilter::Nearest) ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    }
    if (min == SamplerFilter::Nearest && mag == SamplerFilter::Linear) {
        return (mip == SamplerFilter::Nearest) ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
    }
    // min=Linear, mag=Nearest
    return (mip == SamplerFilter::Nearest) ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
}

static GLint toGLMagFilter(SamplerFilter mag) {
    return (mag == SamplerFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
}

static GLint toGLAddressMode(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat:            return GL_REPEAT;
    case SamplerAddressMode::MirroredRepeat:    return GL_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge:       return GL_CLAMP_TO_EDGE;
    case SamplerAddressMode::ClampToBorder:     return GL_CLAMP_TO_BORDER;
    case SamplerAddressMode::MirrorClampToEdge: return GL_MIRROR_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

static GLenum toGLCompareFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never:        return GL_NEVER;
    case CompareFunc::Less:         return GL_LESS;
    case CompareFunc::Equal:        return GL_EQUAL;
    case CompareFunc::LessEqual:    return GL_LEQUAL;
    case CompareFunc::Greater:      return GL_GREATER;
    case CompareFunc::NotEqual:     return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always:       return GL_ALWAYS;
    }
    return GL_NEVER;
}

// ============================================================
// GLSampler
// ============================================================

GLSampler::GLSampler(const SamplerDesc& desc)
    : m_desc(desc)
{
    glGenSamplers(1, &m_handle);

    glSamplerParameteri(m_handle, GL_TEXTURE_MIN_FILTER,
                        toGLFilter(desc.minFilter, desc.magFilter, desc.mipFilter));
    glSamplerParameteri(m_handle, GL_TEXTURE_MAG_FILTER,
                        toGLMagFilter(desc.magFilter));
    glSamplerParameteri(m_handle, GL_TEXTURE_WRAP_S, toGLAddressMode(desc.addressU));
    glSamplerParameteri(m_handle, GL_TEXTURE_WRAP_T, toGLAddressMode(desc.addressV));
    glSamplerParameteri(m_handle, GL_TEXTURE_WRAP_R, toGLAddressMode(desc.addressW));

    glSamplerParameterf(m_handle, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
    glSamplerParameterf(m_handle, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(m_handle, GL_TEXTURE_MAX_LOD, desc.maxLod);

    if (desc.anisotropyEnable) {
        glSamplerParameterf(m_handle, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAniso);
    }

    if (desc.compareEnable) {
        glSamplerParameteri(m_handle, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_handle, GL_TEXTURE_COMPARE_FUNC, toGLCompareFunc(desc.compareFunc));
    } else {
        glSamplerParameteri(m_handle, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    if (desc.addressU == SamplerAddressMode::ClampToBorder ||
        desc.addressV == SamplerAddressMode::ClampToBorder ||
        desc.addressW == SamplerAddressMode::ClampToBorder) {
        glSamplerParameterfv(m_handle, GL_TEXTURE_BORDER_COLOR, desc.borderColor);
    }

    GL_CHECK();
}

GLSampler::~GLSampler() {
    if (m_handle) {
        glDeleteSamplers(1, &m_handle);
        m_handle = 0;
    }
}

} // namespace MulanGeo::Engine
