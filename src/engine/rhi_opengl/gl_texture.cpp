#include "detail/gl_texture.h"

#include <cstring>
#include <string>

namespace mulan::engine {

// ============================================================
// 格式映射表
// ============================================================

GLenum GLTexture::toGLInternalFormat(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm: return GL_RGBA8;
    case TextureFormat::BGRA8_UNorm: return GL_RGBA8;  // GL 无专用 BGRA 内部格式
    case TextureFormat::R8_UNorm: return GL_R8;
    case TextureFormat::RGBA8_sRGB: return GL_SRGB8_ALPHA8;
    case TextureFormat::BGRA8_sRGB: return GL_SRGB8_ALPHA8;
    case TextureFormat::RGBA16_Float: return GL_RGBA16F;
    case TextureFormat::R16_Float: return GL_R16F;
    case TextureFormat::RG16_Float: return GL_RG16F;
    case TextureFormat::RGBA32_Float: return GL_RGBA32F;
    case TextureFormat::R32_Float: return GL_R32F;
    case TextureFormat::RG32_Float: return GL_RG32F;
    case TextureFormat::D16_UNorm: return GL_DEPTH_COMPONENT16;
    case TextureFormat::D24_UNorm_S8_UInt: return GL_DEPTH24_STENCIL8;
    case TextureFormat::D32_Float: return GL_DEPTH_COMPONENT32F;
    case TextureFormat::D32_Float_S8X24_UInt: return GL_DEPTH32F_STENCIL8;
    case TextureFormat::BC1_RGBA_UNorm: return 0x83F1;  // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    case TextureFormat::BC3_RGBA_UNorm: return 0x83F3;  // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    case TextureFormat::BC5_RG_UNorm: return GL_COMPRESSED_RG_RGTC2;
    case TextureFormat::BC7_RGBA_UNorm: return GL_COMPRESSED_RGBA_BPTC_UNORM;
    case TextureFormat::BC7_RGBA_sRGB: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
    default:
        LOG_WARN("[OpenGL] Unknown texture format {}; defaulting to GL_RGBA8", static_cast<int>(fmt));
        return GL_RGBA8;
    }
}

GLenum GLTexture::toGLBaseFormat(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::R8_UNorm:
    case TextureFormat::R16_Float:
    case TextureFormat::R32_Float: return GL_RED;

    case TextureFormat::RG16_Float:
    case TextureFormat::RG32_Float: return GL_RG;

    case TextureFormat::D16_UNorm:
    case TextureFormat::D32_Float: return GL_DEPTH_COMPONENT;

    case TextureFormat::D24_UNorm_S8_UInt:
    case TextureFormat::D32_Float_S8X24_UInt: return GL_DEPTH_STENCIL;

    case TextureFormat::BGRA8_UNorm:
    case TextureFormat::BGRA8_sRGB: return GL_BGRA;

    default: return GL_RGBA;
    }
}

GLenum GLTexture::toGLType(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm:
    case TextureFormat::BGRA8_UNorm:
    case TextureFormat::RGBA8_sRGB:
    case TextureFormat::BGRA8_sRGB:
    case TextureFormat::R8_UNorm: return GL_UNSIGNED_BYTE;

    case TextureFormat::RGBA16_Float:
    case TextureFormat::R16_Float:
    case TextureFormat::RG16_Float: return GL_HALF_FLOAT;

    case TextureFormat::RGBA32_Float:
    case TextureFormat::R32_Float:
    case TextureFormat::RG32_Float:
    case TextureFormat::D32_Float: return GL_FLOAT;

    case TextureFormat::D16_UNorm: return GL_UNSIGNED_SHORT;

    case TextureFormat::D24_UNorm_S8_UInt: return GL_UNSIGNED_INT_24_8;

    case TextureFormat::D32_Float_S8X24_UInt: return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;

    default: return GL_UNSIGNED_BYTE;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

GLTexture::GLTexture(const TextureDesc& desc) : desc_(desc) {
    create();
}

GLTexture::GLTexture(const TextureDesc& desc, GLuint existingHandle) : desc_(desc), handle_(existingHandle) {
    // 从 target 推断（仅 Texture2D 路径供 GLRenderTarget 使用）
    target_ = desc_.sampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    internal_format_ = toGLInternalFormat(desc_.format);
}

GLTexture::~GLTexture() {
    if (handle_) {
        glDeleteTextures(1, &handle_);
        handle_ = 0;
    }
}

// ============================================================
// 创建 GL 纹理对象
// ============================================================

void GLTexture::create() {
    // 确定 GL target
    bool isMultisample = desc_.sampleCount > 1;
    switch (desc_.dimension) {
    case TextureDimension::Texture1D: target_ = GL_TEXTURE_1D; break;
    case TextureDimension::Texture2D: target_ = isMultisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; break;
    case TextureDimension::Texture3D: target_ = GL_TEXTURE_3D; break;
    case TextureDimension::TextureCube: target_ = GL_TEXTURE_CUBE_MAP; break;
    default: target_ = GL_TEXTURE_2D; break;
    }

    internal_format_ = toGLInternalFormat(desc_.format);
    GLsizei mips = static_cast<GLsizei>(desc_.mipLevels);

    glCreateTextures(target_, 1, &handle_);
    if (!handle_) {
        LOG_ERROR("[OpenGL] Texture creation failed: glCreateTextures returned 0, name={}", desc_.name);
        return;
    }

    // 使用 DSA 不可变存储（GL 4.5+）
    switch (target_) {
    case GL_TEXTURE_1D: glTextureStorage1D(handle_, mips, internal_format_, static_cast<GLsizei>(desc_.width)); break;

    case GL_TEXTURE_2D:
    case GL_TEXTURE_CUBE_MAP:
        glTextureStorage2D(handle_, mips, internal_format_, static_cast<GLsizei>(desc_.width),
                           static_cast<GLsizei>(desc_.height));
        break;

    case GL_TEXTURE_2D_MULTISAMPLE:
        // MSAA 纹理无 mip
        glTextureStorage2DMultisample(handle_, static_cast<GLsizei>(desc_.sampleCount), internal_format_,
                                      static_cast<GLsizei>(desc_.width), static_cast<GLsizei>(desc_.height), GL_TRUE);
        break;

    case GL_TEXTURE_3D:
        glTextureStorage3D(handle_, mips, internal_format_, static_cast<GLsizei>(desc_.width),
                           static_cast<GLsizei>(desc_.height), static_cast<GLsizei>(desc_.depth));
        break;

    default: break;
    }

    // 默认采样参数
    if (target_ != GL_TEXTURE_2D_MULTISAMPLE) {
        glTextureParameteri(handle_, GL_TEXTURE_MIN_FILTER, mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(handle_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(handle_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(handle_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (target_ == GL_TEXTURE_3D || target_ == GL_TEXTURE_CUBE_MAP) {
            glTextureParameteri(handle_, GL_TEXTURE_WRAP_R, GL_REPEAT);
        }
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Texture creation failed: error=0x{:X}, name={}", err, desc_.name);
        glDeleteTextures(1, &handle_);
        handle_ = 0;
    }
}

// ============================================================
// 像素数据上传
// ============================================================

void GLTexture::upload(uint32_t mipLevel, const void* data) {
    if (!handle_ || !data)
        return;
    if (target_ != GL_TEXTURE_2D && target_ != GL_TEXTURE_1D && target_ != GL_TEXTURE_3D) {
        LOG_ERROR("[OpenGL] Texture upload rejected: use uploadCubeFace for cube textures");
        return;
    }

    GLenum baseFormat = toGLBaseFormat(desc_.format);
    GLenum type = toGLType(desc_.format);

    // 计算当前 mip 层的尺寸
    GLsizei w = static_cast<GLsizei>(std::max(1u, desc_.width >> mipLevel));
    GLsizei h = static_cast<GLsizei>(std::max(1u, desc_.height >> mipLevel));
    GLsizei d = static_cast<GLsizei>(std::max(1u, desc_.depth >> mipLevel));

    switch (target_) {
    case GL_TEXTURE_1D: glTextureSubImage1D(handle_, static_cast<GLint>(mipLevel), 0, w, baseFormat, type, data); break;
    case GL_TEXTURE_2D:
        glTextureSubImage2D(handle_, static_cast<GLint>(mipLevel), 0, 0, w, h, baseFormat, type, data);
        break;
    case GL_TEXTURE_3D:
        glTextureSubImage3D(handle_, static_cast<GLint>(mipLevel), 0, 0, 0, w, h, d, baseFormat, type, data);
        break;
    default: break;
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Texture upload failed: mip={}, error=0x{:X}", mipLevel, err);
    }
}

void GLTexture::uploadCubeFace(uint32_t face, uint32_t mipLevel, const void* data) {
    if (!handle_ || !data)
        return;
    if (target_ != GL_TEXTURE_CUBE_MAP) {
        LOG_ERROR("[OpenGL] Cube texture upload rejected: texture is not a cube map");
        return;
    }
    if (face > 5) {
        LOG_ERROR("[OpenGL] Cube texture upload rejected: face {} is out of range", face);
        return;
    }

    GLenum baseFormat = toGLBaseFormat(desc_.format);
    GLenum type = toGLType(desc_.format);
    GLsizei w = static_cast<GLsizei>(std::max(1u, desc_.width >> mipLevel));
    GLsizei h = static_cast<GLsizei>(std::max(1u, desc_.height >> mipLevel));

    glTextureSubImage3D(handle_, static_cast<GLint>(mipLevel), 0, 0, static_cast<GLint>(face), w, h, 1, baseFormat,
                        type, data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Cube texture upload failed: face={}, mip={}, error=0x{:X}", face, mipLevel, err);
    }
}

void GLTexture::generateMipmaps() {
    if (!handle_ || desc_.mipLevels <= 1)
        return;
    glGenerateTextureMipmap(handle_);
}

}  // namespace mulan::engine
