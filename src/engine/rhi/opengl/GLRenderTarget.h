/**
 * @file GLRenderTarget.h
 * @brief OpenGL 离屏渲染目标实现（FBO）
 * @author hxxcxx
 * @date 2026-04-22
 */

#pragma once

#include "GLCommon.h"
#include "GLTexture.h"
#include "../RenderTarget.h"
#include "../Texture.h"

#include <memory>

namespace mulan::engine {

class GLRenderTarget final : public RenderTarget {
public:
    explicit GLRenderTarget(const RenderTargetDesc& desc);
    ~GLRenderTarget();

    const RenderTargetDesc& desc() const override { return m_desc; }

    Texture* colorTexture() override { return m_colorTexture.get(); }
    Texture* depthTexture() override { return m_depthTexture.get(); }

    void resize(uint32_t width, uint32_t height) override;

    /// FBO 句柄（供外部 blit 使用）
    GLuint fbo() const { return m_fbo; }

    bool isValid() const { return m_fbo != 0; }

    uint64_t nativeRenderPassHandle() const override { return m_fbo; }

private:
    void createResources();
    void destroyResources();

    static GLenum toGLInternalFormat(TextureFormat fmt);
    static GLenum toGLDepthAttachment(TextureFormat fmt);

    RenderTargetDesc           m_desc;
    GLuint                     m_fbo = 0;
    std::unique_ptr<GLTexture> m_colorTexture;
    std::unique_ptr<GLTexture> m_depthTexture;
};

} // namespace mulan::Engine
