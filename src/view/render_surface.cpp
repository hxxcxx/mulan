/**
 * @file render_surface.cpp
 * @brief RenderSurface 实现
 * @date 2026-07-03
 */

#include "render_surface.h"

#include "mulan/engine/rhi/device.h"

#include <cstdio>
#include <cstring>

namespace mulan::view {

RenderSurface::~RenderSurface() {
    // 资源由 shutdown() 显式释放；这里兜底 reset。
}

bool RenderSurface::initWindowSurface(engine::RHIDevice& device,
                                      const ViewConfig& config, int width, int height) {
    if (swapchain_ || render_target_) return true;

    width_  = width;
    height_ = height;

    engine::NativeWindowHandle window = config.toNativeWindowHandle();
    if (!window.valid()) return false;

    engine::RenderConfig renderCfg = config.toRenderConfig();

    engine::SwapChainDesc scDesc;
    scDesc.width       = static_cast<uint32_t>(width);
    scDesc.height      = static_cast<uint32_t>(height);
    scDesc.format      = engine::TextureFormat::BGRA8_UNorm;
    scDesc.bufferCount = config.bufferCount;
    scDesc.sampleCount = renderCfg.sampleCount();
    scDesc.vsync       = config.vsync;
    std::memcpy(scDesc.clearColor, renderCfg.clearColor, sizeof(scDesc.clearColor));
    scDesc.clearDepth  = renderCfg.clearDepth;

    auto sc = device.createSwapChain(scDesc);
    if (!sc) return false;
    swapchain_ = std::move(*sc);
    return true;
}

bool RenderSurface::initOffscreenSurface(engine::RHIDevice& device, int width, int height) {
    if (swapchain_ || render_target_) return true;

    width_  = width;
    height_ = height;

    engine::RenderTargetDesc rtDesc;
    rtDesc.width       = static_cast<uint32_t>(width);
    rtDesc.height      = static_cast<uint32_t>(height);
    rtDesc.colorFormat = engine::TextureFormat::RGBA8_UNorm;
    rtDesc.depthFormat = engine::TextureFormat::D24_UNorm_S8_UInt;
    rtDesc.hasDepth    = true;

    auto rt = device.createRenderTarget(rtDesc);
    if (!rt) return false;
    render_target_ = std::move(*rt);

    uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
    auto sb = device.createBuffer(
        engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
    if (!sb) {
        render_target_.reset();
        return false;
    }
    staging_buffer_ = std::move(*sb);
    return true;
}

void RenderSurface::shutdown(engine::RHIDevice& device) {
    if (!swapchain_ && !render_target_) return;
    device.waitIdle();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    width_ = 0;
    height_ = 0;
}

void RenderSurface::resize(engine::RHIDevice& device, int width, int height) {
    width_  = width;
    height_ = height;

    device.waitIdle();

    if (render_target_) {
        render_target_->resize(width, height);

        // 重建 readback staging buffer
        staging_buffer_.reset();
        uint32_t pixelBytes = static_cast<uint32_t>(width) * height * 4;
        auto sb = device.createBuffer(
            engine::BufferDesc::staging(pixelBytes, "ReadbackStaging"));
        if (!sb) {
            std::fprintf(stderr, "[RenderSurface] resize staging buffer failed: %s\n",
                         sb.error().message.c_str());
        } else {
            staging_buffer_ = std::move(*sb);
        }
    } else if (swapchain_) {
        device.clearCaches();
        swapchain_->resize(width, height);
    }
}

bool RenderSurface::readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_) return false;

    device.waitIdle();

    auto cmdResult = device.createCommandList();
    if (!cmdResult) {
        std::fprintf(stderr, "[RenderSurface] readbackPixels createCommandList: %s\n",
                     cmdResult.error().message.c_str());
        return false;
    }
    auto cmd = std::move(*cmdResult);
    cmd->begin();
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::CopySrc);
    cmd->copyTextureToBuffer(render_target_->colorTexture(), staging_buffer_.get());
    cmd->end();

    device.executeCommandList(cmd.get());
    device.waitIdle();

    uint32_t byteSize = static_cast<uint32_t>(width_) * height_ * 4;
    pixels.resize(byteSize);
    return staging_buffer_->readback(0, byteSize, pixels.data());
}

engine::TextureFormat RenderSurface::colorFormat(engine::RHIDevice& /*device*/) const {
    return render_target_
        ? render_target_->colorFormat()
        : (swapchain_ ? swapchain_->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
}

engine::TextureFormat RenderSurface::depthFormat(engine::RHIDevice& /*device*/) const {
    return render_target_
        ? render_target_->depthFormat()
        : (swapchain_ ? swapchain_->depthFormat() : engine::TextureFormat::D32_Float);
}

} // namespace mulan::view
