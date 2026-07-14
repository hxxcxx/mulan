/**
 * @file gl_render_target.h
 * @brief OpenGL 离屏渲染目标实现（FBO）
 * @author hxxcxx
 * @date 2026-04-22
 */

#pragma once

#include "gl_common.h"
#include "gl_texture.h"
#include "../../rhi/render_target.h"
#include "../../rhi/texture.h"

#include <memory>

namespace mulan::engine {

class GLRenderTarget final : public RenderTarget {
public:
    explicit GLRenderTarget(const RenderTargetDesc& desc);
    ~GLRenderTarget();

    const RenderTargetDesc& desc() const override { return desc_; }

    Texture* colorTexture() override { return color_texture_.get(); }
    Texture* depthTexture() override { return depth_texture_.get(); }
    RenderPassBeginInfo renderPassBeginInfo() override;

    core::Result<void> resize(uint32_t width, uint32_t height) override;

    /// FBO 句柄（供外部 blit 使用）
    GLuint fbo() const { return fbo_; }

    bool isValid() const { return fbo_ != 0; }

private:
    core::Result<void> createResources();
    void destroyResources();

    static GLenum toGLInternalFormat(TextureFormat fmt);
    static GLenum toGLDepthAttachment(TextureFormat fmt);

    RenderTargetDesc desc_;
    GLuint fbo_ = 0;
    std::unique_ptr<GLTexture> color_texture_;
    std::unique_ptr<GLTexture> msaa_color_texture_;
    std::unique_ptr<GLTexture> depth_texture_;
};

}  // namespace mulan::engine
