#include "image.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace mulan::core {

// ============================================================
// 构造
// ============================================================

Image::Image(uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels)
    : width_(w)
    , height_(h)
    , format_(fmt)
    , pixels_(std::move(pixels)) {
}

// ============================================================
// 查询
// ============================================================

bool Image::valid() const {
    return width_ > 0 && height_ > 0
        && format_ != PixelFormat::Unknown
        && pixels_.size() == static_cast<size_t>(width_) * height_ * bytesPerPixel(format_);
}

const uint8_t* Image::scanline(uint32_t row) const {
    if (row >= height_) return nullptr;
    return pixels_.data() + static_cast<size_t>(row) * rowBytes();
}

// ============================================================
// 变换
// ============================================================

void Image::flipVertically() {
    if (!valid()) return;
    uint32_t rb = rowBytes();
    std::vector<uint8_t> tmp(rb);
    for (uint32_t top = 0, bot = height_ - 1; top < bot; ++top, --bot) {
        uint8_t* pTop = pixels_.data() + top * rb;
        uint8_t* pBot = pixels_.data() + bot * rb;
        std::memcpy(tmp.data(), pTop, rb);
        std::memcpy(pTop, pBot, rb);
        std::memcpy(pBot, tmp.data(), rb);
    }
}

std::shared_ptr<Image> Image::toRGBA() const {
    if (format_ == PixelFormat::RGBA8) {
        // 已经是 RGBA8，返回 copy
        auto copy = std::make_shared<Image>();
        copy->width_  = width_;
        copy->height_ = height_;
        copy->format_ = format_;
        copy->pixels_ = pixels_;
        return copy;
    }

    if (!valid()) return nullptr;

    const uint32_t srcBPP = bytesPerPixel(format_);
    const size_t pixelCount = static_cast<size_t>(width_) * height_;
    std::vector<uint8_t> rgba(pixelCount * 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* src = pixels_.data() + i * srcBPP;
        uint8_t*       dst = rgba.data() + i * 4;

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
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            dst[3] = 255;
            break;
        default:
            break;
        }
    }

    return createFromBuffer(width_, height_, PixelFormat::RGBA8, std::move(rgba));
}

std::vector<uint8_t> Image::detachPixels() {
    width_  = 0;
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

std::expected<void, Error> Image::savePNGExpected(std::string_view path) const {
    if (!valid()) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Cannot save an invalid image."));
    }
    std::string p(path);
    int stride = static_cast<int>(rowBytes());
    int comps  = static_cast<int>(bytesPerPixel(format_));
    const int ok = stbi_write_png(p.c_str(),
        static_cast<int>(width_), static_cast<int>(height_),
        comps, pixels_.data(), stride);
    if (ok == 0) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to save PNG image."));
    }
    return {};
}

bool Image::saveBMP(std::string_view path) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_bmp(p.c_str(),
        static_cast<int>(width_), static_cast<int>(height_),
        comps, pixels_.data()) != 0;
}

bool Image::saveTGA(std::string_view path) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_tga(p.c_str(),
        static_cast<int>(width_), static_cast<int>(height_),
        comps, pixels_.data()) != 0;
}

bool Image::saveJPG(std::string_view path, int quality) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(format_));
    return stbi_write_jpg(p.c_str(),
        static_cast<int>(width_), static_cast<int>(height_),
        comps, pixels_.data(), quality) != 0;
}

// ============================================================
// 工厂：从文件加载
// ============================================================

std::shared_ptr<Image> Image::load(std::string_view path) {
    auto result = loadExpected(path);
    return result ? *result : nullptr;
}

std::expected<std::shared_ptr<Image>, Error> Image::loadExpected(std::string_view path) {
    auto image = load(path, 0);
    if (!image || !image->valid()) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to load image."));
    }
    return image;
}

std::shared_ptr<Image> Image::load(std::string_view path, int forceChannels) {
    std::string p(path);
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(true);

    unsigned char* raw = stbi_load(p.c_str(), &w, &h, &ch, forceChannels);
    if (!raw) return nullptr;

    int outCh = (forceChannels > 0) ? forceChannels : ch;
    size_t total = static_cast<size_t>(w) * h * outCh;

    PixelFormat fmt = PixelFormat::Unknown;
    switch (outCh) {
    case 1: fmt = PixelFormat::R8;    break;
    case 2: fmt = PixelFormat::RG8;   break;
    case 3: fmt = PixelFormat::RGB8;  break;
    case 4: fmt = PixelFormat::RGBA8; break;
    }

    std::vector<uint8_t> pixels(raw, raw + total);
    stbi_image_free(raw);

    return createFromBuffer(static_cast<uint32_t>(w),
                            static_cast<uint32_t>(h),
                            fmt, std::move(pixels));
}

std::shared_ptr<Image> Image::loadFromMemory(const uint8_t* data, size_t size) {
    return loadFromMemory(data, size, 0);
}

std::shared_ptr<Image> Image::loadFromMemory(const uint8_t* data, size_t size, int forceChannels) {
    if (!data || size == 0) return nullptr;

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(true);

    unsigned char* raw = stbi_load_from_memory(
        data, static_cast<int>(size), &w, &h, &ch, forceChannels);
    if (!raw) return nullptr;

    int outCh = (forceChannels > 0) ? forceChannels : ch;
    size_t total = static_cast<size_t>(w) * h * outCh;

    PixelFormat fmt = PixelFormat::Unknown;
    switch (outCh) {
    case 1: fmt = PixelFormat::R8;    break;
    case 2: fmt = PixelFormat::RG8;   break;
    case 3: fmt = PixelFormat::RGB8;  break;
    case 4: fmt = PixelFormat::RGBA8; break;
    }

    std::vector<uint8_t> pixels(raw, raw + total);
    stbi_image_free(raw);

    return createFromBuffer(static_cast<uint32_t>(w),
                            static_cast<uint32_t>(h),
                            fmt, std::move(pixels));
}

// ============================================================
// 工厂：从缓冲区构建
// ============================================================

std::shared_ptr<Image> Image::createFromBuffer(
    uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels) {
    return std::shared_ptr<Image>(new Image(w, h, fmt, std::move(pixels)));
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
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
}

FloatImage::FloatImage(uint32_t w, uint32_t h, uint32_t channels, std::vector<float> pixels)
    : width_(w)
    , height_(h)
    , channels_(channels)
    , pixels_(std::move(pixels)) {
}

bool FloatImage::valid() const {
    return width_ > 0 && height_ > 0 && channels_ > 0 &&
           pixels_.size() == static_cast<size_t>(width_) * height_ * channels_;
}

std::shared_ptr<FloatImage> FloatImage::loadHDR(std::string_view path, int forceChannels) {
    auto result = loadHDRExpected(path, forceChannels);
    return result ? *result : nullptr;
}

std::expected<std::shared_ptr<FloatImage>, Error>
FloatImage::loadHDRExpected(std::string_view path, int forceChannels) {
    std::string p(path);
    int w = 0, h = 0, ch = 0;
    float* raw = stbi_loadf(p.c_str(), &w, &h, &ch, forceChannels);
    if (!raw) {
        return std::unexpected(Error::make(ErrorCode::Io, "Failed to load HDR image."));
    }

    const int outCh = (forceChannels > 0) ? forceChannels : ch;
    if (w <= 0 || h <= 0 || outCh <= 0) {
        stbi_image_free(raw);
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Loaded HDR image has invalid dimensions."));
    }
    const size_t total = static_cast<size_t>(w) * h * outCh;
    std::vector<float> pixels(raw, raw + total);
    stbi_image_free(raw);

    return std::make_shared<FloatImage>(static_cast<uint32_t>(w),
                                        static_cast<uint32_t>(h),
                                        static_cast<uint32_t>(outCh),
                                        std::move(pixels));
}

} // namespace mulan::core
