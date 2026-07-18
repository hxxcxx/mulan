#include "capture_image_encoder.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace mulan::view {
namespace {

core::PixelFormat capturePixelFormat(engine::TextureFormat format) {
    switch (format) {
    case engine::TextureFormat::R8_UNorm: return core::PixelFormat::R8;
    case engine::TextureFormat::RGBA8_UNorm:
    case engine::TextureFormat::RGBA8_sRGB:
    case engine::TextureFormat::BGRA8_UNorm:
    case engine::TextureFormat::BGRA8_sRGB: return core::PixelFormat::RGBA8;
    default: return core::PixelFormat::Unknown;
    }
}

bool isBGRA(engine::TextureFormat format) {
    return format == engine::TextureFormat::BGRA8_UNorm || format == engine::TextureFormat::BGRA8_sRGB;
}

}  // namespace

Result<std::shared_ptr<core::Image>> CaptureImageEncoder::toImage(const engine::RenderCaptureResult& result) {
    MULAN_PROFILE_ZONE();

    const auto pixelFormat = capturePixelFormat(result.format);
    if (pixelFormat == core::PixelFormat::Unknown || result.width == 0 || result.height == 0) {
        return std::unexpected(
                Error::make(ErrorCode::NotSupported, "Capture format cannot be exported as an 8-bit image."));
    }

    const uint32_t bytesPerPixel = core::bytesPerPixel(pixelFormat);
    const uint32_t tightRowBytes = result.width * bytesPerPixel;
    const uint32_t sourceRowBytes = result.rowBytes ? result.rowBytes : tightRowBytes;
    if (sourceRowBytes < tightRowBytes) {
        return std::unexpected(
                Error::make(ErrorCode::InvalidArg, "Capture rowBytes is smaller than the tight image row size."));
    }
    const std::size_t requiredBytes = static_cast<std::size_t>(sourceRowBytes) * result.height;
    if (result.pixels.size() < requiredBytes) {
        return std::unexpected(
                Error::make(ErrorCode::InvalidArg, "Capture pixel buffer is smaller than the declared image size."));
    }

    std::vector<uint8_t> pixels(static_cast<std::size_t>(tightRowBytes) * result.height);
    for (uint32_t y = 0; y < result.height; ++y) {
        const auto* src = result.pixels.data() + static_cast<std::size_t>(y) * sourceRowBytes;
        auto* dst = pixels.data() + static_cast<std::size_t>(y) * tightRowBytes;
        std::copy(src, src + tightRowBytes, dst);
    }

    if (isBGRA(result.format)) {
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }

    return core::Image::createFromBuffer(result.width, result.height, pixelFormat, std::move(pixels));
}

ResultVoid CaptureImageEncoder::savePNG(const engine::RenderCaptureResult& result, std::string_view path) {
    MULAN_PROFILE_ZONE();

    auto image = toImage(result);
    if (!image)
        return std::unexpected(image.error());
    return (*image)->savePNGExpected(path);
}

ResultVoid CaptureImageEncoder::savePNG(const CaptureImage& image, std::string_view path) {
    return savePNG(image.result, path);
}

}  // namespace mulan::view
