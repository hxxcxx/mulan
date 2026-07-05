#include "capture_service.h"

#include "view_context.h"

namespace mulan::view {

std::optional<engine::RenderCaptureResult>
CaptureService::capture(ViewContext& context, const engine::RenderCaptureDesc& desc) const {
    if (!context.isInitialized()) {
        return std::nullopt;
    }
    if (!context.surface().isOffscreen()) {
        return std::nullopt;
    }

    const uint32_t width = desc.width ? desc.width
                                      : static_cast<uint32_t>(context.surface().width());
    const uint32_t height = desc.height ? desc.height
                                        : static_cast<uint32_t>(context.surface().height());
    if (width == 0 || height == 0) {
        return std::nullopt;
    }

    if (context.surface().width() != static_cast<int>(width) ||
        context.surface().height() != static_cast<int>(height)) {
        context.resize(static_cast<int>(width), static_cast<int>(height));
    }

    context.renderFrame();

    engine::RenderCaptureResult result;
    result.width = width;
    result.height = height;
    result.format = desc.format;

    if (desc.readback && !context.readbackPixels(result.pixels)) {
        return std::nullopt;
    }
    return result;
}

} // namespace mulan::view
