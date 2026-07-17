#include "image.h"

#include <mulan/core/profiling/profile.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace mulan::core {

// ============================================================
// 构造
// ============================================================

Image::Image(uint32_t w, uint32_t h, PixelFormat fmt, ImageOrigin origin, std::vector<uint8_t> pixels)
    : width_(w), height_(h), format_(fmt), origin_(origin), pixels_(std::move(pixels)) {
}

// ============================================================
// 查询
// ============================================================

bool Image::valid() const {
    return width_ > 0 && height_ > 0 && format_ != PixelFormat::Unknown &&
           pixels_.size() == static_cast<size_t>(width_) * height_ * bytesPerPixel(format_);
}

const uint8_t* Image::scanline(uint32_t row) const {
    if (row >= height_)
        return nullptr;
    return pixels_.data() + static_cast<size_t>(row) * rowBytes();
}

// ============================================================
// 变换
// ============================================================

std::shared_ptr<Image> Image::convertedOrigin(ImageOrigin target) const {
    if (!valid())
        return nullptr;
    auto pixels = pixels_;
    if (target != origin_) {
        const uint32_t rb = rowBytes();
        for (uint32_t top = 0, bottom = height_ - 1; top < bottom; ++top, --bottom) {
            auto* topRow = pixels.data() + static_cast<size_t>(top) * rb;
            auto* bottomRow = pixels.data() + static_cast<size_t>(bottom) * rb;
            std::swap_ranges(topRow, topRow + rb, bottomRow);
        }
    }
    return createFromBuffer(width_, height_, format_, std::move(pixels), target);
}

std::shared_ptr<Image> Image::toRGBA() const {
    if (format_ == PixelFormat::RGBA8) {
        // 已经是 RGBA8，返回 copy
        auto copy = std::make_shared<Image>();
        copy->width_ = width_;
        copy->height_ = height_;
        copy->format_ = format_;
        copy->pixels_ = pixels_;
        return copy;
    }

    if (!valid())
        return nullptr;

    const uint32_t srcBPP = bytesPerPixel(format_);
    const size_t pixelCount = static_cast<size_t>(width_) * height_;
    std::vector<uint8_t> rgba(pixelCount * 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* src = pixels_.data() + i * srcBPP;
        uint8_t* dst = rgba.data() + i * 4;

        switch (format_) {
        case PixelFormat::R8:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = 255;
            break;
        case PixelFormat::RG8:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = src[1];
            break;
        case PixelFormat::RGB8:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
            break;
        default: break;
        }
    }

    return createFromBuffer(width_, height_, PixelFormat::RGBA8, std::move(rgba), origin_);
}

std::vector<uint8_t> Image::detachPixels() {
    width_ = 0;
    height_ = 0;
    format_ = PixelFormat::Unknown;
    auto out = std::move(pixels_);
    pixels_.clear();
    return out;
}

// ============================================================
// 保存
// ============================================================

bool Image::savePNG(std::string_view path) const {
    return savePNGExpected(path).has_value();
}

ResultVoid Image::savePNGExpected(std::string_view path) const {
    if (!valid()) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Cannot save an invalid image."));
    }
    std::string p(path);
    int stride = static_cast<int>(rowBytes());
    int comps = static_cast<int>(bytesPerPixel(format_));
    const int ok = stbi_write_png(p.c_str(), static_cast<int>(width_), static_cast<int>(height_), comps, pixels_.data(),
                                  stride);
    if (ok == 0) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to save PNG image."));
    }
    return {};
}

bool Image::saveBMP(std::string_view path) const {
    if (!valid())
        return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_bmp(p.c_str(), static_cast<int>(width_), static_cast<int>(height_), comps, pixels_.data()) != 0;
}

bool Image::saveTGA(std::string_view path) const {
    if (!valid())
        return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_tga(p.c_str(), static_cast<int>(width_), static_cast<int>(height_), comps, pixels_.data()) != 0;
}

bool Image::saveJPG(std::string_view path, int quality) const {
    if (!valid())
        return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_jpg(p.c_str(), static_cast<int>(width_), static_cast<int>(height_), comps, pixels_.data(),
                          quality) != 0;
}

// ============================================================
// 工厂：从文件加载
// ============================================================

namespace {
PixelFormat pixelFormatForChannels(int channels) {
    switch (channels) {
    case 1: return PixelFormat::R8;
    case 2: return PixelFormat::RG8;
    case 3: return PixelFormat::RGB8;
    case 4: return PixelFormat::RGBA8;
    default: return PixelFormat::Unknown;
    }
}

Result<std::shared_ptr<Image>> finishDecode(stbi_uc* raw, int width, int height, int sourceChannels,
                                            const ImageDecodeOptions& options) {
    MULAN_PROFILE_ZONE();

    if (!raw)
        return std::unexpected(
                Error::make(ErrorCode::Io, stbi_failure_reason() ? stbi_failure_reason() : "Failed to decode image."));
    const int channels =
            options.channels == ImageChannelLayout::Preserve ? sourceChannels : static_cast<int>(options.channels);
    const auto format = pixelFormatForChannels(channels);
    const bool dimensionsValid = width > 0 && height > 0 && static_cast<uint32_t>(width) <= options.maxWidth &&
                                 static_cast<uint32_t>(height) <= options.maxHeight;
    const uint64_t byteCount =
            dimensionsValid ? static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * channels : 0;
    if (!dimensionsValid || format == PixelFormat::Unknown || byteCount > options.maxDecodedBytes ||
        byteCount > std::numeric_limits<size_t>::max()) {
        stbi_image_free(raw);
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Decoded image exceeds configured limits."));
    }
    std::vector<uint8_t> pixels(raw, raw + static_cast<size_t>(byteCount));
    stbi_image_free(raw);
    auto image = Image::createFromBuffer(static_cast<uint32_t>(width), static_cast<uint32_t>(height), format,
                                         std::move(pixels), ImageOrigin::TopLeft);
    return options.outputOrigin == ImageOrigin::TopLeft ? image : image->convertedOrigin(options.outputOrigin);
}
}  // namespace

Result<std::shared_ptr<Image>> Image::load(std::string_view path, const ImageDecodeOptions& options) {
    std::string p(path);
    int width = 0, height = 0, channels = 0;
    const int requestedChannels = static_cast<int>(options.channels);
    auto* raw = stbi_load(p.c_str(), &width, &height, &channels, requestedChannels);
    return finishDecode(raw, width, height, channels, options);
}

Result<std::shared_ptr<Image>> Image::loadFromMemory(std::span<const std::byte> encoded,
                                                     const ImageDecodeOptions& options) {
    MULAN_PROFILE_ZONE();

    if (encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Encoded image buffer is empty or too large."));
    int width = 0, height = 0, channels = 0;
    const int requestedChannels = static_cast<int>(options.channels);
    auto* raw = [&] {
        MULAN_PROFILE_ZONE_N("stbi_load_from_memory");
        return stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()),
                                     &width, &height, &channels, requestedChannels);
    }();
    return finishDecode(raw, width, height, channels, options);
}

// ============================================================
// 工厂：从缓冲区构建
// ============================================================

std::shared_ptr<Image> Image::createFromBuffer(uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels,
                                               ImageOrigin origin) {
    return std::shared_ptr<Image>(new Image(w, h, fmt, origin, std::move(pixels)));
}

std::shared_ptr<Image> Image::create(uint32_t w, uint32_t h, PixelFormat fmt) {
    size_t total = static_cast<size_t>(w) * h * bytesPerPixel(fmt);
    return createFromBuffer(w, h, fmt, std::vector<uint8_t>(total, 0));
}

// ============================================================
// 文件格式检测
// ============================================================

bool Image::isSupportedFile(std::string_view path) {
    namespace fs = std::filesystem;
    fs::path p(path);
    auto ext = p.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
}

FloatImage::FloatImage(uint32_t w, uint32_t h, uint32_t channels, std::vector<float> pixels)
    : width_(w), height_(h), channels_(channels), pixels_(std::move(pixels)) {
}

bool FloatImage::valid() const {
    return width_ > 0 && height_ > 0 && channels_ > 0 &&
           pixels_.size() == static_cast<size_t>(width_) * height_ * channels_;
}

std::shared_ptr<FloatImage> FloatImage::loadHDR(std::string_view path, int forceChannels,
                                                const ImageDecodeOptions& options) {
    auto result = loadHDRExpected(path, forceChannels, options);
    return result ? *result : nullptr;
}

Result<std::shared_ptr<FloatImage>> FloatImage::loadHDRExpected(std::string_view path, int forceChannels,
                                                                const ImageDecodeOptions& options) {
    std::string p(path);
    int w = 0, h = 0, ch = 0;
    if (forceChannels < 0 || forceChannels > 4 || stbi_info(p.c_str(), &w, &h, &ch) == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Failed to inspect HDR image."));
    }
    const int inspectedChannels = forceChannels > 0 ? forceChannels : ch;
    const bool dimensionsValid = w > 0 && h > 0 && inspectedChannels > 0 &&
                                 static_cast<uint32_t>(w) <= options.maxWidth &&
                                 static_cast<uint32_t>(h) <= options.maxHeight;
    const uint64_t inspectedBytes = dimensionsValid ? static_cast<uint64_t>(w) * static_cast<uint64_t>(h) *
                                                              static_cast<uint64_t>(inspectedChannels) * sizeof(float)
                                                    : 0;
    if (!dimensionsValid || inspectedBytes > options.maxDecodedBytes ||
        inspectedBytes > std::numeric_limits<size_t>::max()) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Decoded HDR image exceeds configured limits."));
    }

    float* raw = stbi_loadf(p.c_str(), &w, &h, &ch, forceChannels);
    if (!raw) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to load HDR image."));
    }

    const int outCh = (forceChannels > 0) ? forceChannels : ch;
    const uint64_t decodedBytes = w > 0 && h > 0 && outCh > 0 ? static_cast<uint64_t>(w) * static_cast<uint64_t>(h) *
                                                                        static_cast<uint64_t>(outCh) * sizeof(float)
                                                              : 0;
    if (w <= 0 || h <= 0 || outCh <= 0 || static_cast<uint32_t>(w) > options.maxWidth ||
        static_cast<uint32_t>(h) > options.maxHeight || decodedBytes > options.maxDecodedBytes ||
        decodedBytes > std::numeric_limits<size_t>::max()) {
        stbi_image_free(raw);
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Decoded HDR image exceeds configured limits."));
    }
    const size_t total = static_cast<size_t>(w) * h * outCh;
    std::vector<float> pixels(raw, raw + total);
    stbi_image_free(raw);

    return std::make_shared<FloatImage>(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                        static_cast<uint32_t>(outCh), std::move(pixels));
}

}  // namespace mulan::core
