/**
 * @file gl_swap_chain.h
 * @brief OpenGL 平台交换链与默认 Framebuffer 实现
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
 * OpenGL 的交换由平台上下文管理，不像 Vulkan 需要显式创建交换链对象。
 * 此类封装了：
 *  - 平台 GLContext 引用（从 GLDevice 传入，不拥有生命周期）
 *  - beginRenderPass: 绑定默认 FBO 并 glClear
 *  - endRenderPass:   noop（或 glFlush）
 *  - present:         调用平台上下文交换缓冲
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

    ResultVoid present() override;

    ResultVoid resize(uint32_t width, uint32_t height) override;

private:
    SwapChainDesc desc_;
    RenderConfig render_config_;

    GLContext& context_;
    std::unique_ptr<GLRenderTarget> msaa_target_;
};

}  // namespace mulan::engine
