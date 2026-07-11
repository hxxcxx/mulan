/**
 * @file render_surface.cpp
 * @brief RenderSurface 实现
 * @date 2026-07-03
 */

#include <mulan/view/scene_sync/render_surface.h>

#include "mulan/rhi/device.h"

#include <cstdio>
#include <utility>

namespace mulan::view {

RenderSurface::~RenderSurface() {
    // 资源由 shutdown() 显式释放；这里兜底 reset。
}

bool RenderSurface::initWindowSurface(engine::RHIDevice& device, const ViewConfig& config, int width, int height) {
    if (swapchain_ || render_target_)
        return true;

    width_ = width;
    height_ = height;

    engine::NativeWindowHandle window = config.toNativeWindowHandle();
    if (!window.valid())
        return false;

    engine::RenderConfig renderCfg = device.renderConfig();

    engine::SwapChainDesc scDesc;
    scDesc.width = static_cast<uint32_t>(width);
    scDesc.height = static_cast<uint32_t>(height);
    scDesc.format = engine::TextureFormat::BGRA8_UNorm;
    scDesc.bufferCount = config.bufferCount;
    scDesc.sampleCount = renderCfg.sampleCount();
    scDesc.vsync = config.vsync;
    std::memcpy(scDesc.clearColor, renderCfg.clearColor, sizeof(scDesc.clearColor));
    scDesc.clearDepth = renderCfg.clearDepth;

    auto sc = device.createSwapChain(scDesc);
    if (!sc)
        return false;
    swapchain_ = std::move(*sc);
    advanceGeneration();
    return true;
}

bool RenderSurface::initOffscreenSurface(engine::RHIDevice& device, int width, int height) {
    RenderSurfaceDesc desc;
    desc.width = width;
    desc.height = height;
    desc.sampleCount = device.renderConfig().sampleCount();
    return initOffscreenSurface(device, desc);
}

bool RenderSurface::initOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc) {
    if (swapchain_ || render_target_)
        return true;
    if (desc.width <= 0 || desc.height <= 0)
        return false;
    const uint32_t bpp = engine::textureFormatBytesPerPixel(desc.colorFormat);
    if (desc.readback && bpp == 0)
        return false;

    offscreen_desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;
    bytes_per_pixel_ = bpp;
    row_bytes_ = static_cast<uint32_t>(width_) * bytes_per_pixel_;

    engine::RenderTargetDesc rtDesc;
    rtDesc.width = static_cast<uint32_t>(desc.width);
    rtDesc.height = static_cast<uint32_t>(desc.height);
    rtDesc.colorFormat = desc.colorFormat;
    rtDesc.depthFormat = desc.depthFormat;
    rtDesc.hasDepth = desc.hasDepth;
    rtDesc.sampleCount = desc.sampleCount;

    auto rt = device.createRenderTarget(rtDesc);
    if (!rt)
        return false;
    render_target_ = std::move(*rt);

    if (!createReadbackBuffer(device)) {
        render_target_.reset();
        return false;
    }
    advanceGeneration();
    return true;
}

bool RenderSurface::configureOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc) {
    if (swapchain_)
        return false;
    if (!render_target_)
        return initOffscreenSurface(device, desc);
    if (offscreenDescMatches(desc))
        return true;

    if (desc.width <= 0 || desc.height <= 0)
        return false;
    const uint32_t nextBytesPerPixel = engine::textureFormatBytesPerPixel(desc.colorFormat);
    if (desc.readback && nextBytesPerPixel == 0)
        return false;

    const bool formatChanged =
            offscreen_desc_.colorFormat != desc.colorFormat || offscreen_desc_.depthFormat != desc.depthFormat ||
            offscreen_desc_.hasDepth != desc.hasDepth || offscreen_desc_.sampleCount != desc.sampleCount;
    const bool readbackChanged = offscreen_desc_.readback != desc.readback;

    device.waitIdle();
    offscreen_desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;
    bytes_per_pixel_ = nextBytesPerPixel;
    row_bytes_ = static_cast<uint32_t>(width_) * bytes_per_pixel_;

    if (formatChanged) {
        // 旧 target 已失效，即使新 target 创建失败也必须让旧帧提交失配。
        advanceGeneration();
        render_target_.reset();
        staging_buffer_.reset();
        return initOffscreenSurface(device, desc);
    }

    render_target_->resize(static_cast<uint32_t>(desc.width), static_cast<uint32_t>(desc.height));
    if (readbackChanged || desc.readback) {
        const bool ready = createReadbackBuffer(device);
        advanceGeneration();
        return ready;
    }
    staging_buffer_.reset();
    advanceGeneration();
    return true;
}

void RenderSurface::shutdown(engine::RHIDevice& device) {
    if (!swapchain_ && !render_target_)
        return;
    device.waitIdle();
    staging_buffer_.reset();
    render_target_.reset();
    swapchain_.reset();
    offscreen_desc_ = {};
    bytes_per_pixel_ = 0;
    row_bytes_ = 0;
    width_ = 0;
    height_ = 0;
    advanceGeneration();
}

void RenderSurface::resize(engine::RHIDevice& device, int width, int height) {
    if (!isInitialized() || width <= 0 || height <= 0) {
        return;
    }
    width_ = width;
    height_ = height;

    device.waitIdle();

    if (render_target_) {
        offscreen_desc_.width = width;
        offscreen_desc_.height = height;
        render_target_->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        row_bytes_ = static_cast<uint32_t>(width_) * bytes_per_pixel_;
        if (!createReadbackBuffer(device)) {
            std::fprintf(stderr, "[RenderSurface] resize staging buffer failed\n");
        }
    } else if (swapchain_) {
        device.clearCaches();
        swapchain_->resize(width, height);
    }
    advanceGeneration();
}

bool RenderSurface::readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_)
        return false;

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

    uint32_t byteSize = row_bytes_ * static_cast<uint32_t>(height_);
    pixels.resize(byteSize);
    return staging_buffer_->readback(0, byteSize, pixels.data());
}

std::optional<RenderSurfaceDesc> RenderSurface::offscreenDesc() const {
    if (!isOffscreen()) {
        return std::nullopt;
    }
    return offscreen_desc_;
}

bool RenderSurface::createReadbackBuffer(engine::RHIDevice& device) {
    staging_buffer_.reset();
    if (!offscreen_desc_.readback)
        return true;
    const uint32_t byteSize = row_bytes_ * static_cast<uint32_t>(height_);
    if (byteSize == 0)
        return false;
    auto sb = device.createBuffer(engine::BufferDesc::staging(byteSize, "ReadbackStaging"));
    if (!sb)
        return false;
    staging_buffer_ = std::move(*sb);
    return true;
}

bool RenderSurface::offscreenDescMatches(const RenderSurfaceDesc& desc) const {
    return render_target_ && offscreen_desc_.width == desc.width && offscreen_desc_.height == desc.height &&
           offscreen_desc_.colorFormat == desc.colorFormat && offscreen_desc_.depthFormat == desc.depthFormat &&
           offscreen_desc_.hasDepth == desc.hasDepth && offscreen_desc_.sampleCount == desc.sampleCount &&
           offscreen_desc_.readback == desc.readback;
}

void RenderSurface::advanceGeneration() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
}

engine::TextureFormat RenderSurface::colorFormat(engine::RHIDevice& /*device*/) const {
    return render_target_ ? render_target_->colorFormat()
                          : (swapchain_ ? swapchain_->colorFormat() : engine::TextureFormat::RGBA8_UNorm);
}

engine::TextureFormat RenderSurface::depthFormat(engine::RHIDevice& /*device*/) const {
    return render_target_ ? render_target_->depthFormat()
                          : (swapchain_ ? swapchain_->depthFormat() : engine::TextureFormat::D32_Float);
}

uint32_t RenderSurface::sampleCount() const {
    if (render_target_)
        return render_target_->desc().sampleCount;
    if (swapchain_)
        return swapchain_->desc().sampleCount;
    return 1;
}

}  // namespace mulan::view
