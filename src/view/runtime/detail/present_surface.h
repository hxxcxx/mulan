/**
 * @file present_surface.h
 * @brief 窗口呈现表面，独占一个 SwapChain 及其尺寸状态。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include "render_runtime_config.h"

#include <mulan/core/result/error.h>
#include <mulan/rhi/swap_chain.h>

#include <cstdint>
#include <memory>

namespace mulan::engine {
class RHIDevice;
}

namespace mulan::view::detail {

class PresentSurface {
public:
    explicit PresentSurface(engine::RHIDevice& device);
    ~PresentSurface();

    PresentSurface(const PresentSurface&) = delete;
    PresentSurface& operator=(const PresentSurface&) = delete;

    ResultVoid init(const PresentSurfaceConfig& config, int width, int height);
    ResultVoid resize(int width, int height);
    void shutdown();

    bool isInitialized() const { return swapchain_ != nullptr; }
    engine::SwapChain& swapChain() const;

    int width() const { return width_; }
    int height() const { return height_; }
    engine::TextureFormat colorFormat() const;
    engine::TextureFormat depthFormat() const;
    bool hasDepth() const;
    uint32_t sampleCount() const;

private:
    engine::RHIDevice& device_;
    std::unique_ptr<engine::SwapChain> swapchain_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace mulan::view::detail
