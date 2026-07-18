/**
 * @file render_surface.cpp
 * @brief RenderSurface 实现
 * @date 2026-07-03
 */

#include "runtime/detail/render_surface.h"

#include <mulan/core/log/log.h>
#include <mulan/rhi/device.h>
#include <mulan/rhi/engine_error_code.h>

#include <cstring>
#include <limits>
#include <utility>

namespace mulan::view::detail {

namespace {
ResultVoid waitForLastSurfaceUse(engine::RHIDevice& device, const char* operation) {
    const engine::SubmissionToken token = device.lastSubmissionToken();
    if (!token)
        return {};
    auto result = device.waitForSubmission(token);
    if (!result) {
        LOG_ERROR("[RenderSurface] GPU wait failed before {}: {}", operation, result.error().message);
        return std::unexpected(result.error());
    }
    return {};
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
    const uint64_t tightRowBytes64 = static_cast<uint64_t>(width) * bytesPerPixel;
    if (tightRowBytes64 > std::numeric_limits<uint32_t>::max())
        return 0;
    const uint32_t tightRowBytes = static_cast<uint32_t>(tightRowBytes64);
    if (device.backend() != engine::GraphicsBackend::D3D12) {
        return tightRowBytes;
    }

    // D3D12 CopyTextureRegion 的 footprint 行距必须按 256 字节对齐。
    constexpr uint32_t kD3D12TextureDataPitchAlignment = 256;
    if (tightRowBytes > std::numeric_limits<uint32_t>::max() - (kD3D12TextureDataPitchAlignment - 1))
        return 0;
    return (tightRowBytes + kD3D12TextureDataPitchAlignment - 1) & ~(kD3D12TextureDataPitchAlignment - 1);
}

struct OffscreenResources {
    std::unique_ptr<engine::RenderTarget> renderTarget;
    std::unique_ptr<engine::Buffer> stagingBuffer;
    uint32_t bytesPerPixel = 0;
    uint32_t rowBytes = 0;
};

Result<OffscreenResources> createOffscreenResources(engine::RHIDevice& device, const RenderSurfaceDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Offscreen surface size must be positive."));

    const uint32_t bytesPerPixel = engine::textureFormatBytesPerPixel(desc.colorFormat);
    if (desc.readback && bytesPerPixel == 0)
        return std::unexpected(engine::makeError(engine::EngineErrorCode::FormatNotSupported,
                                                 "Offscreen readback format has no byte size."));

    const uint32_t rowBytes = readbackRowBytes(device, desc.width, bytesPerPixel);
    if (desc.readback && rowBytes == 0)
        return std::unexpected(engine::makeError(engine::EngineErrorCode::FormatNotSupported,
                                                 "Offscreen readback row pitch is not representable."));

    engine::RenderTargetDesc rtDesc;
    rtDesc.width = static_cast<uint32_t>(desc.width);
    rtDesc.height = static_cast<uint32_t>(desc.height);
    rtDesc.colorFormat = desc.colorFormat;
    rtDesc.depthFormat = desc.depthFormat;
    rtDesc.hasDepth = desc.hasDepth;
    rtDesc.sampleCount = desc.sampleCount;

    auto renderTarget = device.createRenderTarget(rtDesc);
    if (!renderTarget)
        return std::unexpected(renderTarget.error());

    std::unique_ptr<engine::Buffer> stagingBuffer;
    if (desc.readback) {
        const uint64_t byteSize64 = static_cast<uint64_t>(rowBytes) * static_cast<uint32_t>(desc.height);
        if (byteSize64 == 0 || byteSize64 > std::numeric_limits<uint32_t>::max())
            return std::unexpected(
                    Error::make(ErrorCode::InvalidArg, "Offscreen readback buffer size is not representable."));
        auto buffer =
                device.createBuffer(engine::BufferDesc::staging(static_cast<uint32_t>(byteSize64), "ReadbackStaging"));
        if (!buffer)
            return std::unexpected(buffer.error());
        stagingBuffer = std::move(*buffer);
    }

    return OffscreenResources{
        .renderTarget = std::move(*renderTarget),
        .stagingBuffer = std::move(stagingBuffer),
        .bytesPerPixel = bytesPerPixel,
        .rowBytes = rowBytes,
    };
}

}  // namespace

RenderSurface::~RenderSurface() {
    // 资源由 shutdown() 显式释放；这里兜底 reset。
}

ResultVoid RenderSurface::initWindowSurface(engine::RHIDevice& device, const ViewConfig& config, int width,
                                            int height) {
    if (swapchain_ || render_target_)
        return {};
    if (width <= 0 || height <= 0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Window surface size must be positive."));

    const engine::NativeWindowHandle window = config.window;
    if (!window.valid())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Window surface requires a native window."));

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
        return std::unexpected(sc.error());
    swapchain_ = std::move(*sc);
    width_ = width;
    height_ = height;
    return {};
}

ResultVoid RenderSurface::initOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc) {
    if (swapchain_ || render_target_)
        return {};

    auto resources = createOffscreenResources(device, desc);
    if (!resources)
        return std::unexpected(resources.error());

    // 候选 target 与 staging 全部创建成功后才提交成员状态，
    // 因此初次初始化失败不会留下伪尺寸或伪描述。
    render_target_ = std::move(resources->renderTarget);
    staging_buffer_ = std::move(resources->stagingBuffer);
    offscreen_desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;
    bytes_per_pixel_ = resources->bytesPerPixel;
    row_bytes_ = resources->rowBytes;
    return {};
}

ResultVoid RenderSurface::configureOffscreenSurface(engine::RHIDevice& device, const RenderSurfaceDesc& desc) {
    if (swapchain_)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "A window surface cannot become offscreen."));
    if (!render_target_)
        return initOffscreenSurface(device, desc);
    if (offscreenDescMatches(desc))
        return {};

    auto resources = createOffscreenResources(device, desc);
    if (!resources)
        return std::unexpected(resources.error());

    // 先完整构造新资源，再一次性替换当前表面。创建失败时，
    // 旧 target、staging 与尺寸全部保持不变。
    const engine::SubmissionToken token = device.lastSubmissionToken();
    auto oldRenderTarget = std::move(render_target_);
    auto oldStagingBuffer = std::move(staging_buffer_);

    render_target_ = std::move(resources->renderTarget);
    staging_buffer_ = std::move(resources->stagingBuffer);
    offscreen_desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;
    bytes_per_pixel_ = resources->bytesPerPixel;
    row_bytes_ = resources->rowBytes;

    // 旧资源继续按最后一次提交的 token 延迟退役，表面替换本身不阻塞 GPU。
    retireSurfaceResources(device, token, nullptr, std::move(oldRenderTarget), std::move(oldStagingBuffer));
    return {};
}

void RenderSurface::shutdown(engine::RHIDevice& device) {
    if (!swapchain_ && !render_target_)
        return;
    const engine::SubmissionToken token = device.lastSubmissionToken();
    auto oldStagingBuffer = std::move(staging_buffer_);
    auto oldRenderTarget = std::move(render_target_);
    auto oldSwapchain = std::move(swapchain_);
    offscreen_desc_ = {};
    bytes_per_pixel_ = 0;
    row_bytes_ = 0;
    width_ = 0;
    height_ = 0;
    retireSurfaceResources(device, token, std::move(oldSwapchain), std::move(oldRenderTarget),
                           std::move(oldStagingBuffer));
}

ResultVoid RenderSurface::resize(engine::RHIDevice& device, int width, int height) {
    if (!isInitialized() || width <= 0 || height <= 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render surface resize arguments are invalid."));
    }
    if (render_target_) {
        RenderSurfaceDesc desc = offscreen_desc_;
        desc.width = width;
        desc.height = height;
        if (auto configured = configureOffscreenSurface(device, desc); !configured) {
            LOG_ERROR("[RenderSurface] Failed to resize the offscreen surface to {}x{}", width, height);
            return std::unexpected(configured.error());
        }
        return {};
    } else if (swapchain_) {
        if (auto waited = waitForLastSurfaceUse(device, "swapchain resize"); !waited)
            return std::unexpected(waited.error());
        if (auto result = swapchain_->resize(width, height); !result) {
            LOG_ERROR("[RenderSurface] Swapchain resize failed: {}", result.error().message);
            return std::unexpected(result.error());
        }
        width_ = width;
        height_ = height;
        return {};
    }
    return std::unexpected(
            engine::makeError(engine::EngineErrorCode::ResizeFailed, "Render surface has no resizeable target."));
}

ResultVoid RenderSurface::readbackPixels(engine::RHIDevice& device, std::vector<uint8_t>& pixels) {
    if (!render_target_ || !staging_buffer_)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Readback surface is not configured."));

    auto cmdResult = device.createCommandList();
    if (!cmdResult) {
        LOG_ERROR("[RenderSurface] Pixel readback command-list creation failed: {}", cmdResult.error().message);
        return std::unexpected(cmdResult.error());
    }
    auto cmd = std::move(*cmdResult);
    if (auto result = cmd->begin(); !result) {
        LOG_ERROR("[RenderSurface] Pixel readback command recording failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::CopySrc);
    auto copyResult = cmd->copyTextureToBuffer(render_target_->colorTexture(), staging_buffer_.get());
    // 截图表面会复用；恢复下一次渲染通道所需的状态，不能让纹理停留在 CopySrc。
    cmd->transitionResource(render_target_->colorTexture(), engine::ResourceState::RenderTarget);
    if (auto result = cmd->end(); !result) {
        LOG_ERROR("[RenderSurface] Pixel readback command finalization failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    if (!copyResult) {
        LOG_ERROR("[RenderSurface] Pixel readback copy failed: {}", copyResult.error().message);
        return std::unexpected(copyResult.error());
    }

    auto fenceResult = device.createFence(0);
    if (!fenceResult) {
        LOG_ERROR("[RenderSurface] Pixel readback fence creation failed: {}", fenceResult.error().message);
        return std::unexpected(fenceResult.error());
    }
    auto fence = std::move(*fenceResult);
    auto submitResult = device.executeCommandList(cmd.get(), fence.get(), 1);
    if (!submitResult) {
        LOG_ERROR("[RenderSurface] Pixel readback submission failed: {}", submitResult.error().message);
        return std::unexpected(submitResult.error());
    }
    // 不能改为 waitIdle()：命令列表与 fence 必须存活到本次 GPU 复制完成，
    // 但无需排空之后提交到其他队列的工作。
    if (auto waitResult = fence->wait(1); !waitResult) {
        LOG_ERROR("[RenderSurface] Pixel readback wait failed: {}", waitResult.error().message);
        return std::unexpected(waitResult.error());
    }

    uint32_t byteSize = row_bytes_ * static_cast<uint32_t>(height_);
    pixels.resize(byteSize);
    auto readbackResult = staging_buffer_->readback(0, byteSize, pixels.data());
    if (!readbackResult) {
        LOG_ERROR("[RenderSurface] Pixel readback mapping failed: {}", readbackResult.error().message);
        return std::unexpected(readbackResult.error());
    }
    return {};
}

bool RenderSurface::offscreenDescMatches(const RenderSurfaceDesc& desc) const {
    return render_target_ && offscreen_desc_.width == desc.width && offscreen_desc_.height == desc.height &&
           offscreen_desc_.colorFormat == desc.colorFormat && offscreen_desc_.depthFormat == desc.depthFormat &&
           offscreen_desc_.hasDepth == desc.hasDepth && offscreen_desc_.sampleCount == desc.sampleCount &&
           offscreen_desc_.readback == desc.readback;
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

}  // namespace mulan::view::detail
