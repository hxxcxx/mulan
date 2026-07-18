/**
 * @file render_surface.h
 * @brief RenderSurface —— 可渲染表面抽象（窗口交换链 / 离屏渲染目标）
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 从 ViewContext 抽出的表面层。负责持有 SwapChain 或离屏 RenderTarget
 * 以及 readback 用的 staging buffer，处理 resize 与像素回读。
 * 不持有 RHIDevice（由调用方传入），不构建 draw command，不知道 Document/Scene。
 */

#pragma once

#include <mulan/core/result/error.h>
#include <mulan/rhi/buffer.h>
#include <mulan/rhi/render_target.h>
#include <mulan/rhi/swap_chain.h>
#include "render_runtime_config.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::view::detail {

struct RenderSurfaceDesc {
    int width = 0;
    int height = 0;
    engine::TextureFormat colorFormat = engine::TextureFormat::RGBA8_UNorm;
    engine::TextureFormat depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    bool hasDepth = true;
    uint32_t sampleCount = 1;
    bool readback = true;
};

class RenderSurface {
public:
    RenderSurface() = default;
    ~RenderSurface() = default;

    RenderSurface(const RenderSurface&) = delete;
    RenderSurface& operator=(const RenderSurface&) = delete;

    /// 使用原生窗口创建 SwapChain。device 由调用方持有。
    ResultVoid initSwapChain(engine::RHIDevice& device, const RenderSurfaceConfig& config, int width, int height);

    /// 离屏表面：创建 RenderTarget + readback staging buffer。
    ResultVoid initOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc);

    ResultVoid configureOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc);

    void shutdown(engine::RHIDevice& device);

    /// 调整表面尺寸。失败时保留后端错误，使上层能够决定是否丢弃渲染通道。
    ResultVoid resize(engine::RHIDevice& device, int width, int height);

    ResultVoid readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels);

    bool isInitialized() const { return swapchain_ || render_target_; }
    engine::SwapChain* swapChain() const { return swapchain_.get(); }
    engine::RenderTarget* renderTarget() const { return render_target_.get(); }

    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t bytesPerPixel() const { return bytes_per_pixel_; }
    uint32_t rowBytes() const { return row_bytes_; }
    engine::TextureFormat colorFormat() const;
    engine::TextureFormat depthFormat() const;
    bool hasDepth() const;
    uint32_t sampleCount() const;

private:
    bool offscreenDescMatches(const RenderSurfaceDesc& desc) const;

    std::unique_ptr<engine::SwapChain> swapchain_;
    std::unique_ptr<engine::RenderTarget> render_target_;
    std::unique_ptr<engine::Buffer> staging_buffer_;

    RenderSurfaceDesc offscreen_desc_;
    uint32_t bytes_per_pixel_ = 0;
    uint32_t row_bytes_ = 0;

    int width_ = 0;
    int height_ = 0;
};

}  // namespace mulan::view::detail
