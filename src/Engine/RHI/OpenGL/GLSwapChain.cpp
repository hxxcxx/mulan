/**
 * @file GLSwapChain.cpp
 * @brief OpenGL 交换链实现
 * @author terry
 * @date 2026-04-16
 */

#include "GLSwapChain.h"

#include <cstdio>

namespace MulanGeo::engine {

GLSwapChain::GLSwapChain(const SwapChainDesc& desc,
                         const InitParams&    params,
                         const RenderConfig&  renderConfig)
    : m_desc(desc)
    , m_renderConfig(renderConfig)
#ifdef _WIN32
    , m_hdc(params.hdc)
    , m_hwnd(params.hwnd)
#endif
{
    // 设置初始视口
    glViewport(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
}

// ----------------------------------------------------------------
// renderPassBeginInfo — 绑定默认 FBO (0) + 设置清除
// ----------------------------------------------------------------

RenderPassBeginInfo GLSwapChain::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    // OpenGL 使用默认 framebuffer (FBO 0)，不需要 Texture 对象
    info.colorCount   = 1;  // 告知 CommandList 需要清除颜色
    info.nativeHandle = 0;  // FBO 0 = 默认 framebuffer

    if (m_desc.hasDepth) {
        info.depthAttachment.target     = nullptr;  // 默认 FBO 的 depth
        info.depthAttachment.loadAction  = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::Store;
    }

    auto& cc = m_desc.clearColor;
    info.clearColor[0] = cc[0];
    info.clearColor[1] = cc[1];
    info.clearColor[2] = cc[2];
    info.clearColor[3] = cc[3];
    info.clearDepth    = m_desc.clearDepth;
    info.presentSource = true;
    info.width         = m_desc.width;
    info.height        = m_desc.height;
    return info;
}

// ----------------------------------------------------------------
// present — 交换前后缓冲区
// ----------------------------------------------------------------

void GLSwapChain::present() {
#ifdef _WIN32
    if (m_hdc) {
        if (m_desc.vsync) {
            // vsync 由像素格式或 WGL_EXT_swap_control 控制
            // 这里简单通过 SwapBuffers 让驱动决定
        }
        SwapBuffers(m_hdc);
    }
#endif
}

// ----------------------------------------------------------------
// resize — 更新视口（OpenGL 默认 FBO 随窗口自动调整，无需重建）
// ----------------------------------------------------------------

void GLSwapChain::resize(uint32_t width, uint32_t height) {
    m_desc.width  = width;
    m_desc.height = height;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

} // namespace MulanGeo::Engine
