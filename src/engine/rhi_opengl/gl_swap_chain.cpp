#include "detail/gl_swap_chain.h"
#include "../rhi/engine_error_code.h"

#include <algorithm>
#include <iterator>

namespace mulan::engine {

GLSwapChain::GLSwapChain(const SwapChainDesc& desc, GLContext& context) : desc_(desc), context_(context) {
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
    info.colorCount = 1;                        // 告知 CommandList 需要清除颜色
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

ResultVoid GLSwapChain::present() {
    if (msaa_target_) {
        GLuint read_fbo = 0;
        glCreateFramebuffers(1, &read_fbo);
        auto* color = static_cast<GLTexture*>(msaa_target_->colorTexture());
        if (!read_fbo || !color) {
            if (read_fbo)
                glDeleteFramebuffers(1, &read_fbo);
            return std::unexpected(
                    makeError(EngineErrorCode::PresentationFailed, "OpenGL present source framebuffer is unavailable"));
        }
        glNamedFramebufferTexture(read_fbo, GL_COLOR_ATTACHMENT0, color->handle(), 0);
        glNamedFramebufferReadBuffer(read_fbo, GL_COLOR_ATTACHMENT0);
        if (glCheckNamedFramebufferStatus(read_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &read_fbo);
            return std::unexpected(
                    makeError(EngineErrorCode::PresentationFailed, "OpenGL present source framebuffer is incomplete"));
        }
        glBlitNamedFramebuffer(read_fbo, 0, 0, 0, static_cast<GLint>(desc_.width), static_cast<GLint>(desc_.height), 0,
                               0, static_cast<GLint>(desc_.width), static_cast<GLint>(desc_.height),
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDeleteFramebuffers(1, &read_fbo);
        if (glGetError() != GL_NO_ERROR)
            return std::unexpected(makeError(EngineErrorCode::PresentationFailed, "OpenGL present resolve failed"));
    }
    if (!context_.swapBuffers())
        return std::unexpected(makeError(EngineErrorCode::PresentationFailed, "OpenGL swapchain buffer swap failed"));
    return {};
}

// ----------------------------------------------------------------
// resize — 更新视口（OpenGL 默认 FBO 随窗口自动调整，无需重建）
// ----------------------------------------------------------------

ResultVoid GLSwapChain::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "OpenGL swapchain size must be non-zero"));
    if (msaa_target_) {
        auto result = msaa_target_->resize(width, height);
        if (!result)
            return std::unexpected(result.error());
    }
    desc_.width = width;
    desc_.height = height;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return {};
}

}  // namespace mulan::engine
