/**
 * @file gl_swap_chain.h
 * @brief OpenGL 交换链实现（WGL SwapBuffers + 默认 Framebuffer）
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#include "gl_common.h"
#include "gl_context.h"
#include "gl_render_target.h"
#include "../../rhi/swap_chain.h"
#include "../../rhi/window.h"

namespace mulan::engine {

class CommandList;

/**
 * @brief OpenGL 交换链
 *
 * OpenGL 的交换链由平台层管理（WGL SwapBuffers），不像 Vulkan 需要显式创建。
 * 此类封装了：
 *  - HDC/HWND 持有（从 GLDevice 传入，不拥有生命周期）
 *  - beginRenderPass: 绑定默认 FBO 并 glClear
 *  - endRenderPass:   noop（或 glFlush）
 *  - present:         SwapBuffers(hdc)
 *  - resize:          glViewport
 */
class GLSwapChain : public SwapChain {
public:
    GLSwapChain(const SwapChainDesc& desc, GLContext& context, const RenderConfig& renderConfig);

    ~GLSwapChain() override = default;

    // ---- SwapChain 接口 ----

    const SwapChainDesc& desc() const override { return desc_; }

    Texture* currentBackBuffer() override { return msaa_target_ ? msaa_target_->colorTexture() : nullptr; }
    Texture* depthTexture() override { return msaa_target_ ? msaa_target_->depthTexture() : nullptr; }

    RenderPassBeginInfo renderPassBeginInfo() override;

    void present() override;

    void resize(uint32_t width, uint32_t height) override;

private:
    SwapChainDesc desc_;
    RenderConfig render_config_;

    GLContext& context_;
    std::unique_ptr<GLRenderTarget> msaa_target_;
};

}  // namespace mulan::engine
