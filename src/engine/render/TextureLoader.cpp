/**
 * @file TextureLoader.cpp
 * @brief 纹理加载器实现 — 基于 core::Image
 */

#include "TextureLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace mulan::engine {

// ============================================================
// 加载
// ============================================================

LoadedTexture TextureLoader::loadFromFile(const std::string& path,
                                           const TextureLoadOptions& options) const {
    auto image = core::Image::load(path);
    if (!image || !image->valid()) return {};
    return loadFromImage(image, options);
}

LoadedTexture TextureLoader::loadFromImage(const std::shared_ptr<core::Image>& image,
                                            const TextureLoadOptions& options) const {
    if (!image || !image->valid()) return {};

    LoadedTexture result;
    result.width    = image->width();
    result.height   = image->height();

    // 确保是 RGBA8（RHI 纹理需要 4 字节对齐）
    if (image->format() != core::PixelFormat::RGBA8) {
        auto rgba = image->toRGBA();
        if (!rgba) return {};
        result.pixels = rgba->detachPixels();
    } else {
        result.pixels = image->detachPixels();
    }

    result.channels = 4;
    result.format   = toRHITextureFormat(image->format(), false);

    if (options.srgbToLinear) {
        convertSRGBToLinear(result.pixels);
    }

    return result;
}

LoadedTexture TextureLoader::loadFromMemory(const uint8_t* data, size_t size,
                                             const TextureLoadOptions& options) const {
    auto image = core::Image::loadFromMemory(data, size);
    if (!image || !image->valid()) return {};
    return loadFromImage(image, options);
}

bool TextureLoader::isSupportedFormat(const std::string& path) {
    return core::Image::isSupportedFile(path);
}

TextureFormat TextureLoader::guessFormat(const std::string& path,
                                          core::PixelFormat pixelFmt) {
    namespace fs = std::filesystem;
    fs::path p(path);
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool isSrgb = (ext == ".jpg" || ext == ".jpeg");

    if (pixelFmt == core::PixelFormat::R8) return TextureFormat::R8_UNorm;
    return isSrgb ? TextureFormat::RGBA8_sRGB : TextureFormat::RGBA8_UNorm;
}

// ============================================================
// 内部
// ============================================================

TextureFormat TextureLoader::toRHITextureFormat(core::PixelFormat pixelFmt, bool /*isSrgb*/) {
    switch (pixelFmt) {
    case core::PixelFormat::R8:    return TextureFormat::R8_UNorm;
    case core::PixelFormat::RG8:   return TextureFormat::RGBA8_UNorm;
    case core::PixelFormat::RGB8:  return TextureFormat::RGBA8_UNorm;
    case core::PixelFormat::RGBA8: return TextureFormat::RGBA8_UNorm;
    default:                       return TextureFormat::RGBA8_UNorm;
    }
}

void TextureLoader::convertSRGBToLinear(std::vector<uint8_t>& pixels) {
    auto srgb_to_linear = [](uint8_t srgb) -> float {
        float v = srgb / 255.0f;
        return (v <= 0.04045f) ? v / 12.92f
                               : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };

    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i + 0] = static_cast<uint8_t>(srgb_to_linear(pixels[i + 0]) * 255);
        pixels[i + 1] = static_cast<uint8_t>(srgb_to_linear(pixels[i + 1]) * 255);
        pixels[i + 2] = static_cast<uint8_t>(srgb_to_linear(pixels[i + 2]) * 255);
        // alpha 不转换
    }
}

} // namespace mulan::Engine
