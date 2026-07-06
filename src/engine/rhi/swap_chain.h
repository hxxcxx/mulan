/**
 * @file swap_chain.h
 * @brief 交换链，前后缓冲区管理与呈现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "resource.h"
#include "texture.h"
#include "render_types.h"

#include <cstdint>

namespace mulan::engine {

class CommandList;

// ============================================================
// 交换链描述
// ============================================================

struct SwapChainDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
    uint32_t bufferCount = 2;  // 双缓冲 / 三缓冲
    uint32_t sampleCount = 1;  // MSAA sample count（swapchain 后端可按需使用）
    bool vsync = true;
    TextureFormat depthFormat = TextureFormat::D24_UNorm_S8_UInt;
    bool hasDepth = true;

    float clearColor[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
    float clearDepth = 1.0f;
};

// ============================================================
// 交换链基类
// ============================================================

class SwapChain : public RHITrackedResource {
public:
    virtual ~SwapChain() = default;

    virtual const SwapChainDesc& desc() const = 0;

    // 获取当前后缓冲（作为 RenderTarget 绑定）
    virtual Texture* currentBackBuffer() = 0;

    /// Depth 纹理（可能返回 nullptr — 无深度时）
    virtual Texture* depthTexture() = 0;

    // 呈现
    virtual void present() = 0;

    // 窗口大小变化时调用
    virtual void resize(uint32_t width, uint32_t height) = 0;

    /// 构建 RenderPassBeginInfo（供 CommandList::beginRenderPass 使用）
    virtual RenderPassBeginInfo renderPassBeginInfo() {
        RenderPassBeginInfo info;
        auto* color = currentBackBuffer();
        if (color) {
            info.colorAttachments[0].target = color;
            info.colorAttachments[0].loadAction = LoadAction::Clear;
            info.colorAttachments[0].storeAction = StoreAction::Store;
            info.colorCount = 1;
        }
        auto* depth = depthTexture();
        if (depth) {
            info.depthAttachment.target = depth;
            info.depthAttachment.loadAction = LoadAction::Clear;
            info.depthAttachment.storeAction = StoreAction::Store;
        }
        auto& cc = desc().clearColor;
        info.clearColor[0] = cc[0];
        info.clearColor[1] = cc[1];
        info.clearColor[2] = cc[2];
        info.clearColor[3] = cc[3];
        info.clearDepth = desc().clearDepth;
        info.presentSource = true;
        info.width = desc().width;
        info.height = desc().height;
        return info;
    }

    // 便捷查询
    uint32_t width() const { return desc().width; }
    uint32_t height() const { return desc().height; }

    // 格式查询（非虚，通过 desc() 委托）
    TextureFormat colorFormat() const { return desc().format; }
    TextureFormat depthFormat() const { return desc().depthFormat; }
    bool hasDepth() const { return desc().hasDepth; }

protected:
    SwapChain() = default;
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;
};

}  // namespace mulan::engine
