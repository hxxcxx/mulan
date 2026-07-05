#include "capture_service.h"

#include "view_context.h"

#include <optional>
#include <utility>

namespace mulan::view {
namespace {

core::ErrorCode errorCodeFor(CaptureFailureCode code) {
    switch (code) {
    case CaptureFailureCode::ContextNotInitialized:
        return core::ErrorCode::InvalidArg;
    case CaptureFailureCode::SurfaceNotOffscreen:
    case CaptureFailureCode::SurfaceConfigurationFailed:
        return core::ErrorCode::NotSupported;
    case CaptureFailureCode::InvalidSize:
        return core::ErrorCode::InvalidArg;
    case CaptureFailureCode::ReadbackFailed:
        return core::ErrorCode::Io;
    case CaptureFailureCode::None:
        return core::ErrorCode::Generic;
    }
    return core::ErrorCode::Generic;
}

CaptureResult makeFailure(std::string name, CaptureFailureCode code, std::string message) {
    auto error = core::Error::make(errorCodeFor(code), message);
    return CaptureResult{
        .name = std::move(name),
        .result = std::unexpected(error),
        .failure = code,
        .message = std::move(message),
    };
}

} // namespace

class CaptureService::CaptureScope {
public:
    explicit CaptureScope(ViewContext& context)
        : context_(context),
          camera_(context.camera()),
          surface_desc_(context.captureSurfaceSnapshot())
    {
    }

    ~CaptureScope() {
        if (surface_desc_) {
            context_.restoreCaptureSurface(*surface_desc_);
        }
        context_.camera() = camera_;
    }

    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;

private:
    ViewContext& context_;
    engine::Camera camera_;
    std::optional<RenderSurfaceDesc> surface_desc_;
};

uint32_t CaptureService::captureWidth(ViewContext& context,
                                      const engine::RenderCaptureDesc& desc) {
    return desc.width ? desc.width : context.surfaceWidth();
}

uint32_t CaptureService::captureHeight(ViewContext& context,
                                       const engine::RenderCaptureDesc& desc) {
    return desc.height ? desc.height : context.surfaceHeight();
}

std::optional<CaptureResult>
CaptureService::validateCaptureInput(ViewContext& context,
                                     std::string name,
                                     uint32_t width,
                                     uint32_t height) {
    if (!context.isInitialized()) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::ContextNotInitialized,
                           "ViewContext is not initialized.");
    }
    if (!context.isOffscreenSurface()) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::SurfaceNotOffscreen,
                           "Capture requires an offscreen ViewContext.");
    }
    if (width == 0 || height == 0) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::InvalidSize,
                           "Capture width and height must be greater than zero.");
    }
    return std::nullopt;
}

std::optional<CaptureResult>
CaptureService::configureCaptureTarget(ViewContext& context,
                                       const engine::RenderCaptureDesc& desc,
                                       std::string name,
                                       uint32_t width,
                                       uint32_t height) {
    if (!context.configureCaptureSurface(desc, width, height)) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::SurfaceConfigurationFailed,
                           "Capture surface configuration failed.");
    }
    return std::nullopt;
}

core::Result<engine::RenderCaptureResult> CaptureService::readCaptureResult(ViewContext& context,
                                  const engine::RenderCaptureDesc& desc,
                                  uint32_t width,
                                  uint32_t height) {
    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    result.format = desc.format;
    result.bytesPerPixel = engine::textureFormatBytesPerPixel(desc.format);
    result.rowBytes = width * result.bytesPerPixel;

    if (desc.readback && !context.readbackPixels(result.pixels)) {
        return std::unexpected(core::Error::make(core::ErrorCode::Io,
                                                "Capture readback failed."));
    }
    return result;
}

core::Result<engine::RenderCaptureResult> CaptureService::capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const {
    const uint32_t width = captureWidth(context, desc);
    const uint32_t height = captureHeight(context, desc);
    if (auto failure = validateCaptureInput(context, {}, width, height)) {
        return std::unexpected(failure->result.error());
    }

    CaptureScope scope(context);
    if (auto failure = configureCaptureTarget(context, desc, {}, width, height)) {
        return std::unexpected(failure->result.error());
    }

    context.renderFrame(context.snapshotViewState());
    return readCaptureResult(context, desc, width, height);
}

core::Result<CaptureImage> CaptureService::capture(ViewContext& context, const CaptureRequest& request) const {
    const uint32_t width = captureWidth(context, request.desc);
    const uint32_t height = captureHeight(context, request.desc);
    if (auto failure = validateCaptureInput(context, request.name, width, height)) {
        return std::unexpected(failure->result.error());
    }

    CaptureScope scope(context);
    if (auto failure = configureCaptureTarget(context, request.desc, request.name, width, height)) {
        return std::unexpected(failure->result.error());
    }

    engine::Camera camera = request.camera;
    camera.setViewport(static_cast<int>(width), static_cast<int>(height));
    context.renderFrame(context.snapshotViewState(camera, request.visual, width, height));

    auto result = readCaptureResult(context, request.desc, width, height);
    if (!result) return std::unexpected(result.error());
    return CaptureImage{.name = request.name, .result = std::move(*result)};
}

CaptureBatchResult
CaptureService::capture(ViewContext& context, const CaptureBatch& batch) const {
    CaptureBatchResult batchResult;
    batchResult.items.reserve(batch.size());
    for (const auto& request : batch.requests()) {
        const uint32_t width = captureWidth(context, request.desc);
        const uint32_t height = captureHeight(context, request.desc);
        if (auto failure = validateCaptureInput(context, request.name, width, height)) {
            batchResult.items.push_back(std::move(*failure));
            continue;
        }

        CaptureScope scope(context);
        if (auto failure = configureCaptureTarget(context, request.desc, request.name, width, height)) {
            batchResult.items.push_back(std::move(*failure));
            continue;
        }

        engine::Camera camera = request.camera;
        camera.setViewport(static_cast<int>(width), static_cast<int>(height));
        context.renderFrame(context.snapshotViewState(camera, request.visual, width, height));

        auto result = readCaptureResult(context, request.desc, width, height);
        if (!result) {
            batchResult.items.push_back(CaptureResult{
                .name = request.name,
                .result = std::unexpected(result.error()),
                .failure = CaptureFailureCode::ReadbackFailed,
                .message = result.error().message,
            });
            continue;
        }
        batchResult.items.push_back(CaptureResult{
            .name = request.name,
            .result = std::move(*result),
            .failure = CaptureFailureCode::None,
            .message = {},
        });
    }
    return batchResult;
}

} // namespace mulan::view
