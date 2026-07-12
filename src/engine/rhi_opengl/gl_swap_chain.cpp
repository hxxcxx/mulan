#include "detail/gl_swap_chain.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace mulan::engine {

GLSwapChain::GLSwapChain(const SwapChainDesc& desc, GLContext& context, const RenderConfig& renderConfig)
    : desc_(desc), render_config_(renderConfig), context_(context) {
    context_.setSwapInterval(desc_.vsync ? 1 : 0);
    if (desc_.sampleCount > 1) {
        RenderTargetDesc target_desc;
        target_desc.width = desc_.width;
        target_desc.height = desc_.height;
        target_desc.colorFormat = desc_.format;
        target_desc.depthFormat = desc_.depthFormat;
        target_desc.hasDepth = desc_.hasDepth;
        target_desc.sampleCount = desc_.sampleCount;
        std::copy(std::begin(desc_.clearColor), std::end(desc_.clearColor), target_desc.clearColor);
        target_desc.clearDepth = desc_.clearDepth;
        msaa_target_ = std::make_unique<GLRenderTarget>(target_desc);
    }
    // 设置初始视口
    glViewport(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
}

// ----------------------------------------------------------------
// renderPassBeginInfo — 绑定默认 FBO (0) + 设置清除
// ----------------------------------------------------------------

RenderPassBeginInfo GLSwapChain::renderPassBeginInfo() {
    if (msaa_target_) {
        auto info = msaa_target_->renderPassBeginInfo();
        info.presentSource = true;
        std::copy(std::begin(desc_.clearColor), std::end(desc_.clearColor), info.clearColor);
        info.clearDepth = desc_.clearDepth;
        info.width = desc_.width;
        info.height = desc_.height;
        return info;
    }
    RenderPassBeginInfo info;
    // OpenGL 使用默认 framebuffer (FBO 0)，不需要 Texture 对象
    info.colorCount = 1;    // 告知 CommandList 需要清除颜色
    info.nativeHandle = 0;  // FBO 0 = 默认 framebuffer

    if (desc_.hasDepth) {
        info.depthAttachment.target = nullptr;  // 默认 FBO 的 depth
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::Store;
    }

    auto& cc = desc_.clearColor;
    info.clearColor[0] = cc[0];
    info.clearColor[1] = cc[1];
    info.clearColor[2] = cc[2];
    info.clearColor[3] = cc[3];
    info.clearDepth = desc_.clearDepth;
    info.presentSource = true;
    info.width = desc_.width;
    info.height = desc_.height;
    return info;
}

// ----------------------------------------------------------------
// present — 交换前后缓冲区
// ----------------------------------------------------------------

void GLSwapChain::present() {
    if (msaa_target_) {
        GLuint read_fbo = 0;
        glGenFramebuffers(1, &read_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
        auto* color = dynamic_cast<GLTexture*>(msaa_target_->colorTexture());
        if (color) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color->target(), color->handle(), 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, static_cast<GLint>(desc_.width), static_cast<GLint>(desc_.height), 0, 0,
                              static_cast<GLint>(desc_.width), static_cast<GLint>(desc_.height), GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &read_fbo);
    }
    context_.swapBuffers();
}

// ----------------------------------------------------------------
// resize — 更新视口（OpenGL 默认 FBO 随窗口自动调整，无需重建）
// ----------------------------------------------------------------

void GLSwapChain::resize(uint32_t width, uint32_t height) {
    desc_.width = width;
    desc_.height = height;
    if (msaa_target_)
        msaa_target_->resize(width, height);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

}  // namespace mulan::engine
