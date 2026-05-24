/**
 * @file RenderTarget.h
 * @brief 离屏渲染目标抽象 — 可独立于 SwapChain 使用的 framebuffer
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - RenderTarget 提供与 SwapChain 相同的 beginRenderPass/endRenderPass 协议
 *  - 上层代码可以统一处理"渲染到屏幕"和"渲染到纹理"
 *  - color / depth 纹理由 RenderTarget 内部创建并持有
 *  - resize 时自动重建所有资源
 */

#pragma once

#include "Texture.h"
#include "RenderTypes.h"

#include <array>
#include <cstdint>

namespace MulanGeo::engine {

class CommandList;

// ============================================================
// RenderTarget 描述
// ============================================================

struct RenderTargetDesc {
    uint32_t      width       = 0;
    uint32_t      height      = 0;
    TextureFormat colorFormat = TextureFormat::RGBA8_UNorm;
    TextureFormat depthFormat = TextureFormat::D24_UNorm_S8_UInt;
    bool          hasDepth    = true;

    float clearColor[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
    float clearDepth    = 1.0f;
};

// ============================================================
// RenderTarget 基类
// ============================================================

class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    virtual const RenderTargetDesc& desc() const = 0;

    /// Color 纹理（可用于后续采样/回读）
    virtual Texture* colorTexture() = 0;

    /// Depth 纹理（可选）
    virtual Texture* depthTexture() = 0;

    /// 大小变化时重建资源
    virtual void resize(uint32_t width, uint32_t height) = 0;

    /// 构建 RenderPassBeginInfo（供 CommandList::beginRenderPass 使用）
    RenderPassBeginInfo renderPassBeginInfo() {
        RenderPassBeginInfo info;
        auto* color = colorTexture();
        if (color) {
            info.colorAttachments[0].target     = color;
            info.colorAttachments[0].loadAction  = LoadAction::Clear;
            info.colorAttachments[0].storeAction = StoreAction::Store;
            info.colorCount = 1;
        }
        auto* depth = depthTexture();
        if (depth) {
            info.depthAttachment.target     = depth;
            info.depthAttachment.loadAction  = LoadAction::Clear;
            info.depthAttachment.storeAction = StoreAction::Store;
        }
        auto& cc = desc().clearColor;
        info.clearColor[0] = cc[0];
        info.clearColor[1] = cc[1];
        info.clearColor[2] = cc[2];
        info.clearColor[3] = cc[3];
        info.clearDepth    = desc().clearDepth;
        info.presentSource = false;
        info.width         = desc().width;
        info.height        = desc().height;
        info.nativeHandle  = nativeRenderPassHandle();
        return info;
    }

    /// 后端特定句柄（如 GL 的 FBO），默认返回 0
    virtual uint64_t nativeRenderPassHandle() const { return 0; }

    // 便捷查询
    uint32_t width()  const { return desc().width; }
    uint32_t height() const { return desc().height; }

    // 格式查询（非虚，通过 desc() 委托）
    TextureFormat colorFormat() const { return desc().colorFormat; }
    TextureFormat depthFormat() const { return desc().depthFormat; }
    bool          hasDepth()    const { return desc().hasDepth; }

protected:
    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
};

} // namespace MulanGeo::Engine
