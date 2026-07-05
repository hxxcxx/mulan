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

bool prepareOffscreenSurface(ViewContext& context, uint32_t width, uint32_t height) {
    if (!context.isInitialized()) {
        return false;
    }
    if (!context.surface().isOffscreen()) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false;
    }
    if (context.surface().width() != static_cast<int>(width) ||
        context.surface().height() != static_cast<int>(height)) {
        context.resize(static_cast<int>(width), static_cast<int>(height));
    }
    return true;
}

std::optional<engine::RenderCaptureResult>
readCaptureResult(ViewContext& context, const engine::RenderCaptureDesc& desc,
                  uint32_t width, uint32_t height) {
    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    result.format = desc.format;

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
    if (!prepareOffscreenSurface(context, width, height)) return std::nullopt;

    context.renderFrame();
    return readCaptureResult(context, desc, width, height);
}

std::optional<CaptureImage>
CaptureService::capture(ViewContext& context, const CaptureRequest& request) const {
    const uint32_t width = captureWidth(context, request.desc);
    const uint32_t height = captureHeight(context, request.desc);
    if (!prepareOffscreenSurface(context, width, height)) return std::nullopt;

    engine::Camera camera = request.camera;
    camera.setViewport(static_cast<int>(width), static_cast<int>(height));
    context.renderFrame(makeCaptureViewState(camera, request.visual, width, height));

    auto result = readCaptureResult(context, request.desc, width, height);
    if (!result) return std::nullopt;
    return CaptureImage{.name = request.name, .result = std::move(*result)};
}

std::vector<CaptureImage>
CaptureService::capture(ViewContext& context, const CaptureBatch& batch) const {
    std::vector<CaptureImage> images;
    images.reserve(batch.size());
    for (const auto& request : batch.requests()) {
        auto image = capture(context, request);
        if (image) {
            images.push_back(std::move(*image));
        }
    }
    return images;
}

} // namespace mulan::view
