#include "capture_service.h"

#include "view_context.h"

#include <utility>

namespace mulan::view {
namespace {

uint32_t captureWidth(ViewContext& context, const engine::RenderCaptureDesc& desc) {
    return desc.width ? desc.width : static_cast<uint32_t>(context.surface().width());
}

uint32_t captureHeight(ViewContext& context, const engine::RenderCaptureDesc& desc) {
    return desc.height ? desc.height : static_cast<uint32_t>(context.surface().height());
}

CaptureResult makeFailure(std::string name, CaptureFailureCode code, std::string message) {
    return CaptureResult{
        .name = std::move(name),
        .result = std::nullopt,
        .failure = code,
        .message = std::move(message),
    };
}

std::optional<CaptureResult>
validateOffscreenSurface(ViewContext& context,
                         const engine::RenderCaptureDesc& desc,
                         std::string name,
                         uint32_t width,
                         uint32_t height) {
    if (!context.isInitialized()) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::ContextNotInitialized,
                           "ViewContext is not initialized.");
    }
    if (!context.surface().isOffscreen()) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::SurfaceNotOffscreen,
                           "Capture requires an offscreen ViewContext.");
    }
    if (width == 0 || height == 0) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::InvalidSize,
                           "Capture width and height must be greater than zero.");
    }
    if (!context.configureCaptureSurface(desc, width, height)) {
        return makeFailure(std::move(name),
                           CaptureFailureCode::SurfaceConfigurationFailed,
                           "Capture surface configuration failed.");
    }
    return std::nullopt;
}

std::optional<engine::RenderCaptureResult>
readCaptureResult(ViewContext& context, const engine::RenderCaptureDesc& desc,
                  uint32_t width, uint32_t height) {
    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    result.format = desc.format;
    result.bytesPerPixel = engine::textureFormatBytesPerPixel(desc.format);
    result.rowBytes = width * result.bytesPerPixel;

    if (desc.readback && !context.readbackPixels(result.pixels)) {
        return std::nullopt;
    }
    return result;
}

} // namespace

std::optional<engine::RenderCaptureResult>
CaptureService::capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const {
    const uint32_t width = captureWidth(context, desc);
    const uint32_t height = captureHeight(context, desc);
    if (validateOffscreenSurface(context, desc, {}, width, height)) return std::nullopt;

    context.renderFrame();
    return readCaptureResult(context, desc, width, height);
}

std::optional<CaptureImage>
CaptureService::capture(ViewContext& context, const CaptureRequest& request) const {
    const uint32_t width = captureWidth(context, request.desc);
    const uint32_t height = captureHeight(context, request.desc);
    if (validateOffscreenSurface(context, request.desc, request.name, width, height)) return std::nullopt;

    engine::Camera camera = request.camera;
    camera.setViewport(static_cast<int>(width), static_cast<int>(height));
    context.renderFrame(makeCaptureViewState(camera, request.visual, width, height));

    auto result = readCaptureResult(context, request.desc, width, height);
    if (!result) return std::nullopt;
    return CaptureImage{.name = request.name, .result = std::move(*result)};
}

CaptureBatchResult
CaptureService::capture(ViewContext& context, const CaptureBatch& batch) const {
    CaptureBatchResult batchResult;
    batchResult.items.reserve(batch.size());
    for (const auto& request : batch.requests()) {
        const uint32_t width = captureWidth(context, request.desc);
        const uint32_t height = captureHeight(context, request.desc);
        if (auto failure = validateOffscreenSurface(context, request.desc, request.name, width, height)) {
            batchResult.items.push_back(std::move(*failure));
            continue;
        }

        engine::Camera camera = request.camera;
        camera.setViewport(static_cast<int>(width), static_cast<int>(height));
        context.renderFrame(makeCaptureViewState(camera, request.visual, width, height));

        auto result = readCaptureResult(context, request.desc, width, height);
        if (!result) {
            batchResult.items.push_back(makeFailure(request.name,
                                                   CaptureFailureCode::ReadbackFailed,
                                                   "Capture readback failed."));
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
