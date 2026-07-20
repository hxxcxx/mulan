/**
 * @file capture_target.cpp
 * @brief CaptureTarget 实现。
 * @author hxxcxx
 * @date 2026-07-20
 */

#include "detail/capture_target.h"

#include <mulan/core/log/log.h>
#include <mulan/rhi/device.h>
#include <mulan/rhi/engine_error_code.h>

#include <cassert>
#include <limits>
#include <utility>

namespace mulan::view::detail {
namespace {

uint32_t readbackRowBytes(const engine::RHIDevice& device, int width, uint32_t bytesPerPixel) {
    const uint64_t tightRowBytes64 = static_cast<uint64_t>(width) * bytesPerPixel;
    if (tightRowBytes64 > std::numeric_limits<uint32_t>::max())
        return 0;
    const uint32_t tightRowBytes = static_cast<uint32_t>(tightRowBytes64);
    if (device.backend() != engine::GraphicsBackend::D3D12)
        return tightRowBytes;

    constexpr uint32_t kD3D12TextureDataPitchAlignment = 256;
    if (tightRowBytes > std::numeric_limits<uint32_t>::max() - (kD3D12TextureDataPitchAlignment - 1))
        return 0;
    return (tightRowBytes + kD3D12TextureDataPitchAlignment - 1) & ~(kD3D12TextureDataPitchAlignment - 1);
}

struct CaptureResources {
    std::unique_ptr<engine::RenderTarget> renderTarget;
    std::unique_ptr<engine::Buffer> readbackBuffer;
    uint32_t bytesPerPixel = 0;
    uint32_t rowBytes = 0;
};

Result<CaptureResources> createResources(engine::RHIDevice& device, const CaptureTargetDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Capture target size must be positive."));

    const uint32_t bytesPerPixel = engine::textureFormatBytesPerPixel(desc.colorFormat);
    if (desc.readback && bytesPerPixel == 0)
        return std::unexpected(engine::makeError(engine::EngineErrorCode::FormatNotSupported,
                                                 "Capture readback format has no byte size."));

    const uint32_t rowBytes = readbackRowBytes(device, desc.width, bytesPerPixel);
    if (desc.readback && rowBytes == 0)
        return std::unexpected(engine::makeError(engine::EngineErrorCode::FormatNotSupported,
                                                 "Capture readback row pitch is not representable."));

    engine::RenderTargetDesc targetDesc;
    targetDesc.width = static_cast<uint32_t>(desc.width);
    targetDesc.height = static_cast<uint32_t>(desc.height);
    targetDesc.colorFormat = desc.colorFormat;
    targetDesc.depthFormat = desc.depthFormat;
    targetDesc.hasDepth = desc.hasDepth;
    targetDesc.sampleCount = desc.sampleCount;

    auto renderTarget = device.createRenderTarget(targetDesc);
    if (!renderTarget)
        return std::unexpected(renderTarget.error());

    std::unique_ptr<engine::Buffer> readbackBuffer;
    if (desc.readback) {
        const uint64_t byteSize64 = static_cast<uint64_t>(rowBytes) * static_cast<uint32_t>(desc.height);
        if (byteSize64 == 0 || byteSize64 > std::numeric_limits<uint32_t>::max())
            return std::unexpected(
                    Error::make(ErrorCode::InvalidArg, "Capture readback buffer size is not representable."));
        auto buffer =
                device.createBuffer(engine::BufferDesc::staging(static_cast<uint32_t>(byteSize64), "ReadbackStaging"));
        if (!buffer)
            return std::unexpected(buffer.error());
        readbackBuffer = std::move(*buffer);
    }

    return CaptureResources{
        .renderTarget = std::move(*renderTarget),
        .readbackBuffer = std::move(readbackBuffer),
        .bytesPerPixel = bytesPerPixel,
        .rowBytes = rowBytes,
    };
}

void retireResources(engine::RHIDevice& device, engine::SubmissionToken token,
                     std::unique_ptr<engine::RenderTarget> renderTarget,
                     std::unique_ptr<engine::Buffer> readbackBuffer) {
    if ((!renderTarget && !readbackBuffer) || !token)
        return;
    auto result = device.retire(
            token, [renderTarget = std::move(renderTarget), readbackBuffer = std::move(readbackBuffer)]() mutable {
                readbackBuffer.reset();
                renderTarget.reset();
            });
    if (!result)
        LOG_ERROR("[CaptureTarget] Deferred resource release failed: {}", result.error().message);
}

}  // namespace

CaptureTarget::CaptureTarget(engine::RHIDevice& device) : device_(device) {
}

CaptureTarget::~CaptureTarget() {
    shutdown();
}

ResultVoid CaptureTarget::configure(const CaptureTargetDesc& desc) {
    if (descMatches(desc))
        return {};

    auto resources = createResources(device_, desc);
    if (!resources)
        return std::unexpected(resources.error());

    const engine::SubmissionToken token = device_.lastSubmissionToken();
    auto oldRenderTarget = std::move(render_target_);
    auto oldReadbackBuffer = std::move(readback_buffer_);

    render_target_ = std::move(resources->renderTarget);
    readback_buffer_ = std::move(resources->readbackBuffer);
    desc_ = desc;
    bytes_per_pixel_ = resources->bytesPerPixel;
    row_bytes_ = resources->rowBytes;

    retireResources(device_, token, std::move(oldRenderTarget), std::move(oldReadbackBuffer));
    return {};
}

ResultVoid CaptureTarget::readbackPixels(std::vector<uint8_t>& pixels) {
    if (!render_target_ || !readback_buffer_)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Capture target is not configured for readback."));

    auto commandList = device_.createCommandList();
    if (!commandList) {
        LOG_ERROR("[CaptureTarget] Readback command-list creation failed: {}", commandList.error().message);
        return std::unexpected(commandList.error());
    }
    auto command = std::move(*commandList);
    if (auto result = command->begin(); !result) {
        LOG_ERROR("[CaptureTarget] Readback command recording failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    command->transitionResource(render_target_->colorTexture(), engine::ResourceState::CopySrc);
    auto copied = command->copyTextureToBuffer(render_target_->colorTexture(), readback_buffer_.get());
    command->transitionResource(render_target_->colorTexture(), engine::ResourceState::RenderTarget);
    if (auto result = command->end(); !result) {
        LOG_ERROR("[CaptureTarget] Readback command finalization failed: {}", result.error().message);
        return std::unexpected(result.error());
    }
    if (!copied) {
        LOG_ERROR("[CaptureTarget] Texture copy failed: {}", copied.error().message);
        return std::unexpected(copied.error());
    }

    auto fenceResult = device_.createFence(0);
    if (!fenceResult) {
        LOG_ERROR("[CaptureTarget] Readback fence creation failed: {}", fenceResult.error().message);
        return std::unexpected(fenceResult.error());
    }
    auto fence = std::move(*fenceResult);
    if (auto submitted = device_.executeCommandList(command.get(), fence.get(), 1); !submitted) {
        LOG_ERROR("[CaptureTarget] Readback submission failed: {}", submitted.error().message);
        return std::unexpected(submitted.error());
    }
    if (auto waited = fence->wait(1); !waited) {
        LOG_ERROR("[CaptureTarget] Readback wait failed: {}", waited.error().message);
        return std::unexpected(waited.error());
    }

    const uint32_t byteSize = row_bytes_ * static_cast<uint32_t>(desc_.height);
    pixels.resize(byteSize);
    auto readback = readback_buffer_->readback(0, byteSize, pixels.data());
    if (!readback) {
        LOG_ERROR("[CaptureTarget] Readback mapping failed: {}", readback.error().message);
        return std::unexpected(readback.error());
    }
    return {};
}

void CaptureTarget::shutdown() {
    if (!render_target_ && !readback_buffer_)
        return;
    const engine::SubmissionToken token = device_.lastSubmissionToken();
    auto renderTarget = std::move(render_target_);
    auto readbackBuffer = std::move(readback_buffer_);
    desc_ = {};
    bytes_per_pixel_ = 0;
    row_bytes_ = 0;
    retireResources(device_, token, std::move(renderTarget), std::move(readbackBuffer));
}

engine::RenderTarget& CaptureTarget::renderTarget() const {
    assert(render_target_ && "CaptureTarget must be configured before accessing its RenderTarget.");
    return *render_target_;
}

engine::TextureFormat CaptureTarget::colorFormat() const {
    return render_target_ ? render_target_->colorFormat() : engine::TextureFormat::RGBA8_UNorm;
}

bool CaptureTarget::descMatches(const CaptureTargetDesc& desc) const {
    return render_target_ && desc_.width == desc.width && desc_.height == desc.height &&
           desc_.colorFormat == desc.colorFormat && desc_.depthFormat == desc.depthFormat &&
           desc_.hasDepth == desc.hasDepth && desc_.sampleCount == desc.sampleCount && desc_.readback == desc.readback;
}

}  // namespace mulan::view::detail
