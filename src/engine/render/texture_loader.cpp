#include "texture_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace mulan::engine {

// ============================================================
// 加载
// ============================================================

LoadedTexture TextureLoader::loadFromFile(const std::string& path, const TextureLoadOptions& options) const {
    auto image = core::Image::load(path);
    if (!image || !image->valid())
        return {};

    // 调用方未显式指定 sRGB 时，按扩展名推断（jpg/jpeg 是颜色贴图 → sRGB；
    // png 多为数据贴图如 normal/mr，默认 linear）。任一为真即用 sRGB 格式。
    TextureLoadOptions effective = options;
    if (!effective.sRGB && inferSrgb(path))
        effective.sRGB = true;

    return loadFromImage(image, effective);
}

LoadedTexture TextureLoader::loadFromImage(const std::shared_ptr<core::Image>& image,
                                           const TextureLoadOptions& options) const {
    if (!image || !image->valid())
        return {};

    LoadedTexture result;
    result.width = image->width();
    result.height = image->height();

    // 确保是 RGBA8（RHI 纹理需要 4 字节对齐）
    if (image->format() != core::PixelFormat::RGBA8) {
        auto rgba = image->toRGBA();
        if (!rgba)
            return {};
        result.pixels = rgba->detachPixels();
    } else {
        result.pixels = image->detachPixels();
    }

    result.channels = 4;
    // sRGB 意图真正生效：RGBA8 → RGBA8_sRGB（硬件采样自动 sRGB→linear，
    // shader 无需手动转换，避免双重转换导致的颜色偏差）。
    result.format = toRHITextureFormat(image->format(), options.sRGB);

    return result;
}

LoadedTexture TextureLoader::loadFromMemory(const uint8_t* data, size_t size, const TextureLoadOptions& options) const {
    auto image = core::Image::loadFromMemory(data, size);
    if (!image || !image->valid())
        return {};
    return loadFromImage(image, options);
}

bool TextureLoader::isSupportedFormat(const std::string& path) {
    return core::Image::isSupportedFile(path);
}

// ============================================================
// 内部
// ============================================================

TextureFormat TextureLoader::toRHITextureFormat(core::PixelFormat pixelFmt, bool sRGB) {
    switch (pixelFmt) {
    case core::PixelFormat::R8: return TextureFormat::R8_UNorm;      // 数据通道，不转 sRGB
    case core::PixelFormat::RG8: return TextureFormat::RGBA8_UNorm;  // RG 扩展为 RGBA，数据贴图
    case core::PixelFormat::RGB8:
    case core::PixelFormat::RGBA8:
        // 颜色贴图按 sRGB 意图选择格式；硬件采样时自动解码到 linear
        return sRGB ? TextureFormat::RGBA8_sRGB : TextureFormat::RGBA8_UNorm;
    default: return sRGB ? TextureFormat::RGBA8_sRGB : TextureFormat::RGBA8_UNorm;
    }
}

bool TextureLoader::inferSrgb(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path p(path);
    auto ext = p.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // jpg/jpeg 摄影来源 → sRGB；png 多用于 normal/mr/ao 等数据贴图 → 默认 linear
    return ext == ".jpg" || ext == ".jpeg";
}

}  // namespace mulan::engine
