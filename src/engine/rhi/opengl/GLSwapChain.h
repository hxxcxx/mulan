/**
 * @file GLSwapChain.h
 * @brief OpenGL 交换链实现（WGL SwapBuffers + 默认 Framebuffer）
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#include "GLCommon.h"
#include "../SwapChain.h"
#include "../../Window.h"

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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    struct InitParams {
        HDC    hdc   = nullptr;
        HWND   hwnd  = nullptr;
    };
#else
    struct InitParams {};
#endif

    GLSwapChain(const SwapChainDesc& desc,
                const InitParams&    params,
                const RenderConfig&  renderConfig);

    ~GLSwapChain() override = default;

    // ---- SwapChain 接口 ----

    const SwapChainDesc& desc() const override { return m_desc; }

    Texture* currentBackBuffer() override { return nullptr; } // 默认 FBO，无独立 Texture 对象
    Texture* depthTexture() override { return nullptr; }     // 默认 FBO 的 depth buffer

    RenderPassBeginInfo renderPassBeginInfo() override;

    void present() override;

    void resize(uint32_t width, uint32_t height) override;

private:
    SwapChainDesc  m_desc;
    RenderConfig   m_renderConfig;

#ifdef _WIN32
    HDC   m_hdc  = nullptr;
    HWND  m_hwnd = nullptr;
#endif
};

} // namespace mulan::engine
