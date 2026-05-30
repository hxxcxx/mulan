/**
 * @file GLTexture.h
 * @brief OpenGL 纹理实现
 * @author terry
 * @date 2026-04-16
 *
 * 支持以下纹理类型：
 * - Texture2D (GL_TEXTURE_2D)
 * - Texture3D (GL_TEXTURE_3D)
 * - TextureCube (GL_TEXTURE_CUBE_MAP)
 * - Texture1D (GL_TEXTURE_1D)
 * - MSAA Texture2D (GL_TEXTURE_2D_MULTISAMPLE)
 *
 * 支持的用途：
 * - ShaderResource: 着色器采样
 * - RenderTarget: 作为 FBO 颜色附件
 * - DepthStencil: 作为 FBO 深度/模板附件
 */

#pragma once

#include "GLCommon.h"
#include "../Texture.h"

namespace mulan::engine {

class GLTexture final : public Texture {
public:
    explicit GLTexture(const TextureDesc& desc);

    /// 从已有 GL 纹理句柄构造（接管所有权）
    GLTexture(const TextureDesc& desc, GLuint existingHandle);

    ~GLTexture() override;

    const TextureDesc& desc() const override { return m_desc; }

    /// OpenGL 纹理对象句柄
    GLuint handle() const { return m_handle; }

    /// OpenGL 纹理目标 (GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, ...)
    GLenum target() const { return m_target; }

    /// 内部格式
    GLenum internalFormat() const { return m_internalFormat; }

    /// 是否有效
    bool isValid() const { return m_handle != 0; }

    /// 上传像素数据到指定 mip 层（仅 Texture2D）
    /// @param mipLevel  Mip 层级（0 = 基础层）
    /// @param data      像素数据指针（与 TextureFormat 对应的布局）
    void upload(uint32_t mipLevel, const void* data);

    /// 上传像素数据到 Cube 面（仅 TextureCube）
    /// @param face      0-5 对应 +X/-X/+Y/-Y/+Z/-Z
    /// @param mipLevel  Mip 层级
    /// @param data      像素数据
    void uploadCubeFace(uint32_t face, uint32_t mipLevel, const void* data);

    /// 自动生成 Mip 链
    void generateMipmaps();

    // ── 格式转换工具（供 GLRenderTarget 等复用）──

    /// TextureFormat → GL 内部格式
    static GLenum toGLInternalFormat(TextureFormat fmt);
    /// TextureFormat → GL 基础格式（upload 时使用）
    static GLenum toGLBaseFormat(TextureFormat fmt);
    /// TextureFormat → GL 数据类型
    static GLenum toGLType(TextureFormat fmt);

private:
    void create();

    TextureDesc m_desc;
    GLuint      m_handle        = 0;
    GLenum      m_target        = GL_TEXTURE_2D;
    GLenum      m_internalFormat = GL_RGBA8;
};

} // namespace mulan::engine
