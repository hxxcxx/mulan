#include "capture/capture_service.h"

#include <mulan/view/core/view_context.h>

#include <utility>

namespace mulan::view {
namespace {

ErrorCode errorCodeFor(CaptureFailureCode code) {
    switch (code) {
    case CaptureFailureCode::ContextNotInitialized: return ErrorCode::InvalidArg;
    case CaptureFailureCode::InvalidSize: return ErrorCode::InvalidArg;
    case CaptureFailureCode::CaptureFailed: return ErrorCode::Generic;
    case CaptureFailureCode::None: return ErrorCode::Generic;
    }
    return ErrorCode::Generic;
}

CaptureResult makeFailure(std::string name, CaptureFailureCode code, std::string message) {
    auto error = Error::make(errorCodeFor(code), message);
    return CaptureResult{
        .name = std::move(name),
        .result = std::unexpected(error),
        .failure = code,
        .message = std::move(message),
    };
}

engine::RenderCaptureDesc normalizedDesc(engine::RenderCaptureDesc desc, uint32_t width, uint32_t height) {
    desc.width = width;
    desc.height = height;
    return desc;
}

}  // namespace

uint32_t CaptureService::captureWidth(ViewContext& context, const engine::RenderCaptureDesc& desc) {
    return desc.width ? desc.width : context.surfaceWidth();
}

uint32_t CaptureService::captureHeight(ViewContext& context, const engine::RenderCaptureDesc& desc) {
    return desc.height ? desc.height : context.surfaceHeight();
}

std::optional<CaptureResult> CaptureService::validateCaptureInput(ViewContext& context, std::string name,
                                                                  uint32_t width, uint32_t height) {
    if (!context.isInitialized()) {
        return makeFailure(std::move(name), CaptureFailureCode::ContextNotInitialized,
                           "ViewContext is not initialized.");
    }
    if (width == 0 || height == 0) {
        return makeFailure(std::move(name), CaptureFailureCode::InvalidSize,
                           "Capture width and height must be greater than zero.");
    }
    return std::nullopt;
}

Result<engine::RenderCaptureResult> CaptureService::capture(ViewContext& context,
                                                            const engine::RenderCaptureDesc& desc) const {
    const uint32_t width = captureWidth(context, desc);
    const uint32_t height = captureHeight(context, desc);
    if (auto failure = validateCaptureInput(context, {}, width, height)) {
        return std::unexpected(failure->result.error());
    }
    return context.captureFrame(context.snapshotViewState(width, height), normalizedDesc(desc, width, height));
}

Result<CaptureImage> CaptureService::capture(ViewContext& context, const CaptureRequest& request) const {
    const uint32_t width = captureWidth(context, request.desc);
    const uint32_t height = captureHeight(context, request.desc);
    if (auto failure = validateCaptureInput(context, request.name, width, height)) {
        return std::unexpected(failure->result.error());
    }

    engine::Camera camera = request.camera;
    camera.setViewport(static_cast<int>(width), static_cast<int>(height));
    auto result = context.captureFrame(context.snapshotViewState(camera, request.visual, width, height),
                                       normalizedDesc(request.desc, width, height));
    if (!result) {
        return std::unexpected(result.error());
    }
    return CaptureImage{ .name = request.name, .result = std::move(*result) };
}

CaptureBatchResult CaptureService::capture(ViewContext& context, const CaptureBatch& batch) const {
    CaptureBatchResult batchResult;
    batchResult.items.reserve(batch.size());
    for (const auto& request : batch.requests()) {
        auto result = capture(context, request);
        if (!result) {
            batchResult.items.push_back(CaptureResult{
                    .name = request.name,
                    .result = std::unexpected(result.error()),
                    .failure = CaptureFailureCode::CaptureFailed,
                    .message = result.error().message,
            });
            continue;
        }
        batchResult.items.push_back(CaptureResult{
                .name = request.name,
                .result = std::move(result->result),
                .failure = CaptureFailureCode::None,
                .message = {},
        });
    }
    return batchResult;
}

}  // namespace mulan::view
