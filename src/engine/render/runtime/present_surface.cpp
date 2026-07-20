/**
 * @file present_surface.cpp
 * @brief PresentSurface 实现。
 * @author hxxcxx
 * @date 2026-07-20
 */

#include "detail/present_surface.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include "../../rhi/device.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace mulan::engine::detail {
namespace {

ResultVoid waitForLastPresent(engine::RHIDevice& device) {
    const engine::SubmissionToken token = device.lastSubmissionToken();
    if (!token)
        return {};
    auto result = device.waitForSubmission(token);
    if (!result) {
        LOG_ERROR("[PresentSurface] GPU wait failed before SwapChain resize: {}", result.error().message);
        return std::unexpected(result.error());
    }
    return {};
}

void retireSwapChain(engine::RHIDevice& device, engine::SubmissionToken token,
                     std::unique_ptr<engine::SwapChain> swapchain) {
    if (!swapchain || !token)
        return;

    auto result = device.retire(token, [swapchain = std::move(swapchain)]() mutable { swapchain.reset(); });
    if (!result)
        LOG_ERROR("[PresentSurface] Deferred SwapChain release failed: {}", result.error().message);
}

}  // namespace

PresentSurface::PresentSurface(engine::RHIDevice& device) : device_(device) {
}

PresentSurface::~PresentSurface() {
    shutdown();
}

ResultVoid PresentSurface::init(const RenderSurfaceConfig& config, int width, int height) {
    MULAN_PROFILE_ZONE();

    if (swapchain_)
        return {};
    if (width <= 0 || height <= 0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "SwapChain size must be positive."));
    if (!config.window.valid())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "SwapChain requires a native window."));

    engine::SwapChainDesc desc;
    desc.window = config.window;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.format = engine::TextureFormat::BGRA8_UNorm;
    desc.bufferCount = config.bufferCount;
    const uint32_t maxSampleCount = (std::max) (device_.capabilities().maxSampleCount, 1u);
    desc.sampleCount = (std::min) (config.sampleCount(), maxSampleCount);
    desc.vsync = config.vsync;
    std::memcpy(desc.clearColor, config.clearColor, sizeof(desc.clearColor));
    desc.clearDepth = config.clearDepth;
    desc.hasDepth = config.depthBuffer;
    // 保持既有交换链语义：窗口表面的深度附件固定使用 D24S8。
    // 配置边界拆分只调整所有权，不改变传入 RHI 的附件格式。
    desc.depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;

    auto swapchain = device_.createSwapChain(desc);
    if (!swapchain)
        return std::unexpected(swapchain.error());

    swapchain_ = std::move(*swapchain);
    width_ = width;
    height_ = height;
    return {};
}

ResultVoid PresentSurface::resize(int width, int height) {
    if (!swapchain_ || width <= 0 || height <= 0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Present surface resize is invalid."));
    if (auto waited = waitForLastPresent(device_); !waited)
        return std::unexpected(waited.error());
    if (auto resized = swapchain_->resize(width, height); !resized) {
        LOG_ERROR("[PresentSurface] SwapChain resize failed: {}", resized.error().message);
        return std::unexpected(resized.error());
    }
    width_ = width;
    height_ = height;
    return {};
}

void PresentSurface::shutdown() {
    if (!swapchain_)
        return;
    const engine::SubmissionToken token = device_.lastSubmissionToken();
    auto swapchain = std::move(swapchain_);
    width_ = 0;
    height_ = 0;
    retireSwapChain(device_, token, std::move(swapchain));
}

engine::SwapChain& PresentSurface::swapChain() const {
    assert(swapchain_ && "PresentSurface must be initialized before accessing its SwapChain.");
    return *swapchain_;
}

engine::TextureFormat PresentSurface::colorFormat() const {
    return swapchain_ ? swapchain_->colorFormat() : engine::TextureFormat::RGBA8_UNorm;
}

engine::TextureFormat PresentSurface::depthFormat() const {
    return swapchain_ ? swapchain_->depthFormat() : engine::TextureFormat::D32_Float;
}

bool PresentSurface::hasDepth() const {
    return swapchain_ && swapchain_->hasDepth();
}

uint32_t PresentSurface::sampleCount() const {
    return swapchain_ ? swapchain_->desc().sampleCount : 1;
}

}  // namespace mulan::engine::detail
