#include "detail/gl_sampler.h"

namespace mulan::engine {

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
    case SamplerAddressMode::Repeat: return GL_REPEAT;
    case SamplerAddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
    case SamplerAddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
    case SamplerAddressMode::MirrorClampToEdge: return GL_MIRROR_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

static GLenum toGLCompareFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return GL_NEVER;
    case CompareFunc::Less: return GL_LESS;
    case CompareFunc::Equal: return GL_EQUAL;
    case CompareFunc::LessEqual: return GL_LEQUAL;
    case CompareFunc::Greater: return GL_GREATER;
    case CompareFunc::NotEqual: return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always: return GL_ALWAYS;
    }
    return GL_NEVER;
}

// ============================================================
// GLSampler
// ============================================================

GLSampler::GLSampler(const SamplerDesc& desc) : desc_(desc) {
    glCreateSamplers(1, &handle_);
    if (!handle_)
        return;

    glSamplerParameteri(handle_, GL_TEXTURE_MIN_FILTER, toGLFilter(desc.minFilter, desc.magFilter, desc.mipFilter));
    glSamplerParameteri(handle_, GL_TEXTURE_MAG_FILTER, toGLMagFilter(desc.magFilter));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_S, toGLAddressMode(desc.addressU));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_T, toGLAddressMode(desc.addressV));
    glSamplerParameteri(handle_, GL_TEXTURE_WRAP_R, toGLAddressMode(desc.addressW));

    glSamplerParameterf(handle_, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
    glSamplerParameterf(handle_, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(handle_, GL_TEXTURE_MAX_LOD, desc.maxLod);

    if (desc.anisotropyEnable && glExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        glSamplerParameterf(handle_, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAniso);
    }

    if (desc.compareEnable) {
        glSamplerParameteri(handle_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(handle_, GL_TEXTURE_COMPARE_FUNC, toGLCompareFunc(desc.compareFunc));
    } else {
        glSamplerParameteri(handle_, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    if (desc.addressU == SamplerAddressMode::ClampToBorder || desc.addressV == SamplerAddressMode::ClampToBorder ||
        desc.addressW == SamplerAddressMode::ClampToBorder) {
        glSamplerParameterfv(handle_, GL_TEXTURE_BORDER_COLOR, desc.borderColor);
    }
}

GLSampler::~GLSampler() {
    if (handle_) {
        glDeleteSamplers(1, &handle_);
        handle_ = 0;
    }
}

}  // namespace mulan::engine
