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

#include "mulan/engine/rhi/buffer.h"
#include "mulan/engine/rhi/render_target.h"
#include "mulan/engine/rhi/swap_chain.h"
#include "view_config.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::view {

struct RenderSurfaceDesc {
    int width = 0;
    int height = 0;
    engine::TextureFormat colorFormat = engine::TextureFormat::RGBA8_UNorm;
    engine::TextureFormat depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    bool hasDepth = true;
    bool readback = true;
};

class RenderSurface {
public:
    RenderSurface() = default;
    ~RenderSurface();

    RenderSurface(const RenderSurface&) = delete;
    RenderSurface& operator=(const RenderSurface&) = delete;

    /// 窗口表面：创建 SwapChain。device 由调用方持有。
    bool initWindowSurface(engine::RHIDevice& device,
                           const ViewConfig& config, int width, int height);

    /// 离屏表面：创建 RenderTarget + readback staging buffer。
    bool initOffscreenSurface(engine::RHIDevice& device, int width, int height);
    bool initOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc);

    bool configureOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc);

    void shutdown(engine::RHIDevice& device);

    void resize(engine::RHIDevice& device, int width, int height);

    bool readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels);

    bool isInitialized() const { return swapchain_ || render_target_; }
    bool isOffscreen()   const { return !swapchain_ && static_cast<bool>(render_target_); }

    engine::SwapChain*   swapChain()    const { return swapchain_.get(); }
    engine::RenderTarget* renderTarget() const { return render_target_.get(); }

    int width()  const { return width_; }
    int height() const { return height_; }
    uint32_t bytesPerPixel() const { return bytes_per_pixel_; }
    uint32_t rowBytes() const { return row_bytes_; }
    std::optional<RenderSurfaceDesc> offscreenDesc() const;

    engine::TextureFormat colorFormat(engine::RHIDevice& device) const;
    engine::TextureFormat depthFormat(engine::RHIDevice& device) const;

private:
    bool createReadbackBuffer(engine::RHIDevice& device);
    bool offscreenDescMatches(const RenderSurfaceDesc& desc) const;

    std::unique_ptr<engine::SwapChain>    swapchain_;
    std::unique_ptr<engine::RenderTarget> render_target_;
    std::unique_ptr<engine::Buffer>       staging_buffer_;

    RenderSurfaceDesc offscreen_desc_;
    uint32_t bytes_per_pixel_ = 0;
    uint32_t row_bytes_ = 0;

    int width_  = 0;
    int height_ = 0;
};

} // namespace mulan::view
