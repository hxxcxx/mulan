/**
 * @file render_surface.cpp
 * @brief RenderSurface 实现
 * @date 2026-07-03
 */

#include <mulan/view/scene_sync/render_surface.h>

#include <mulan/core/log/log.h>
#include "mulan/rhi/device.h"

#include <utility>

namespace mulan::view {

namespace {
void mergeLastUse(engine::SubmissionToken& latest, const engine::RHITrackedResource* resource) {
    if (!resource)
        return;
    const engine::SubmissionToken candidate = resource->lastUseToken();
    if (candidate &&
        (!latest || (candidate.deviceGeneration == latest.deviceGeneration && candidate.value > latest.value)))
        latest = candidate;
}

engine::SubmissionToken surfaceLastUse(engine::SwapChain* swapchain, engine::RenderTarget* renderTarget,
                                       engine::Buffer* stagingBuffer) {
    engine::SubmissionToken latest{};
    mergeLastUse(latest, swapchain);
    mergeLastUse(latest, renderTarget);
    mergeLastUse(latest, stagingBuffer);
    if (renderTarget) {
        mergeLastUse(latest, renderTarget->colorTexture());
        mergeLastUse(latest, renderTarget->depthTexture());
    }
    return latest;
}

bool waitForLastSurfaceUse(engine::RHIDevice& device, engine::SwapChain* swapchain, engine::RenderTarget* renderTarget,
                           engine::Buffer* stagingBuffer, const char* operation) {
    const engine::SubmissionToken token = surfaceLastUse(swapchain, renderTarget, stagingBuffer);
    if (!token)
        return true;
    auto result = device.waitForSubmission(token);
    if (!result) {
        LOG_ERROR("[RenderSurface] GPU wait failed before {}: {}", operation, result.error().message);
        return false;
    }
    return true;
}

void retireSurfaceResources(engine::RHIDevice& device, engine::SubmissionToken token,
                            std::unique_ptr<engine::SwapChain> swapchain,
                            std::unique_ptr<engine::RenderTarget> renderTarget,
                            std::unique_ptr<engine::Buffer> stagingBuffer) {
    if (!swapchain && !renderTarget && !stagingBuffer)
        return;
    if (!token)
        return;

    auto result = device.retire(token, [swapchain = std::move(swapchain), renderTarget = std::move(renderTarget),
                                        stagingBuffer = std::move(stagingBuffer)]() mutable {
        stagingBuffer.reset();
        renderTarget.reset();
        swapchain.reset();
    });
    if (!result)
        LOG_ERROR("[RenderSurface] Deferred surface release failed: {}", result.error().message);
}

uint32_t readbackRowBytes(const engine::RHIDevice& device, int width, uint32_t bytesPerPixel) {
    const uint32_t tightRowBytes = static_cast<uint32_t>(width) * bytesPerPixel;
    if (device.backend() != engine::GraphicsBackend::D3D12) {
        return tightRowBytes;
    }

    // D3D12 CopyTextureRegion 的 footprint 行距必须按 256 字节对齐。
    constexpr uint32_t kD3D12TextureDataPitchAlignment = 256;
    return (tightRowBytes + kD3D12TextureDataPitchAlignment - 1) & ~(kD3D12TextureDataPitchAlignment - 1);
}

}  // namespace

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
    scDesc.window = window;
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
    row_bytes_ = readbackRowBytes(device, width_, bytes_per_pixel_);

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

    const engine::SubmissionToken token = surfaceLastUse(nullptr, render_target_.get(), staging_buffer_.get());
    auto oldRenderTarget = std::move(render_target_);
    auto oldStagingBuffer = std::move(staging_buffer_);

    // 离屏资源可以版本化替换，无需阻塞等待 GPU。旧 target 即使在新建失败时
    // 也保持失效，generation 会让已排队的旧帧提交被上层丢弃。
    const bool initialized = initOffscreenSurface(device, desc);
    if (!initialized)
        advanceGeneration();
    retireSurfaceResources(device, token, nullptr, std::move(oldRenderTarget), std::move(oldStagingBuffer));
    return initialized;
}

void RenderSurface::shutdown(engine::RHIDevice& device) {
    if (!swapchain_ && !render_target_)
        return;
    const engine::SubmissionToken token = surfaceLastUse(swapchain_.get(), render_target_.get(), staging_buffer_.get());
    auto oldStagingBuffer = std::move(staging_buffer_);
    auto oldRenderTarget = std::move(render_target_);
    auto oldSwapchain = std::move(swapchain_);
    offscreen_desc_ = {};
    bytes_per_pixel_ = 0;
    row_bytes_ = 0;
    width_ = 0;
    height_ = 0;
    advanceGeneration();
    retireSurfaceResources(device, token, std::move(oldSwapchain), std::move(oldRenderTarget),
                           std::move(oldStagingBuffer));
}

void RenderSurface::resize(engine::RHIDevice& device, int width, int height) {
    if (!isInitialized() || width <= 0 || height <= 0) {
        return;
    }
    if (render_target_) {
        RenderSurfaceDesc desc = offscreen_desc_;
        desc.width = width;
        desc.height = height;
        if (!configureOffscreenSurface(device, desc))
            LOG_ERROR("[RenderSurface] Failed to resize the offscreen surface to {}x{}", width, height);
    } else if (swapchain_) {
        if (!waitForLastSurfaceUse(device, swapchain_.get(), nullptr, nullptr, "swapchain resize"))
            return;
        width_ = width;
        height_ = height;
        device.clearCaches();
        swapchain_->resize(width, height);
        advanceGeneration();
    }
}

bool RenderSurface::readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_)
        return false;

    auto cmdResult = device.createCommandList();
    if (!cmdResult) {
        LOG_ERROR("[RenderSurface] Pixel readback command-list creation failed: {}", cmdResult.error().message);
        return false;
    }
    auto cmd = std::move(*cmdResult);
    cmd->begin();
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::CopySrc);
    const bool copyRecorded = cmd->copyTextureToBuffer(render_target_->colorTexture(), staging_buffer_.get());
    // 截图表面会复用；恢复下一次渲染通道所需的状态，不能让纹理停留在 CopySrc。
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::RenderTarget);
    cmd->end();
    if (!copyRecorded)
        return false;

    auto fenceResult = device.createFence(0);
    if (!fenceResult) {
        LOG_ERROR("[RenderSurface] Pixel readback fence creation failed: {}", fenceResult.error().message);
        return false;
    }
    auto fence = std::move(*fenceResult);
    auto submitResult = device.executeCommandList(cmd.get(), fence.get(), 1);
    if (!submitResult) {
        LOG_ERROR("[RenderSurface] Pixel readback submission failed: {}", submitResult.error().message);
        return false;
    }
    // 不能改为 waitIdle()：命令列表与 fence 必须存活到本次 GPU 复制完成，
    // 但无需排空之后提交到其他队列的工作。
    fence->wait(1);

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
