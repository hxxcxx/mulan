/**
 * @file Image.cpp
 * @brief Image 实现 — stb_image / stb_image_write 后端
 * @author hxxcxx
 * @date 2026-04-23
 */

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS (16384)

#include "Image.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace MulanGeo::core {

// ============================================================
// 构造
// ============================================================

Image::Image(uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels)
    : m_width(w)
    , m_height(h)
    , m_format(fmt)
    , m_pixels(std::move(pixels)) {
}

// ============================================================
// 查询
// ============================================================

bool Image::valid() const {
    return m_width > 0 && m_height > 0
        && m_format != PixelFormat::Unknown
        && m_pixels.size() == static_cast<size_t>(m_width) * m_height * bytesPerPixel(m_format);
}

const uint8_t* Image::scanline(uint32_t row) const {
    if (row >= m_height) return nullptr;
    return m_pixels.data() + static_cast<size_t>(row) * rowBytes();
}

// ============================================================
// 变换
// ============================================================

void Image::flipVertically() {
    if (!valid()) return;
    uint32_t rb = rowBytes();
    std::vector<uint8_t> tmp(rb);
    for (uint32_t top = 0, bot = m_height - 1; top < bot; ++top, --bot) {
        uint8_t* pTop = m_pixels.data() + top * rb;
        uint8_t* pBot = m_pixels.data() + bot * rb;
        std::memcpy(tmp.data(), pTop, rb);
        std::memcpy(pTop, pBot, rb);
        std::memcpy(pBot, tmp.data(), rb);
    }
}

std::shared_ptr<Image> Image::toRGBA() const {
    if (m_format == PixelFormat::RGBA8) {
        // 已经是 RGBA8，返回 copy
        auto copy = std::make_shared<Image>();
        copy->m_width  = m_width;
        copy->m_height = m_height;
        copy->m_format = m_format;
        copy->m_pixels = m_pixels;
        return copy;
    }

    if (!valid()) return nullptr;

    const uint32_t srcBPP = bytesPerPixel(m_format);
    const size_t pixelCount = static_cast<size_t>(m_width) * m_height;
    std::vector<uint8_t> rgba(pixelCount * 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* src = m_pixels.data() + i * srcBPP;
        uint8_t*       dst = rgba.data() + i * 4;

        switch (m_format) {
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

    return createFromBuffer(m_width, m_height, PixelFormat::RGBA8, std::move(rgba));
}

std::vector<uint8_t> Image::detachPixels() {
    m_width  = 0;
    m_height = 0;
    m_format = PixelFormat::Unknown;
    auto out = std::move(m_pixels);
    m_pixels.clear();
    return out;
}

// ============================================================
// 保存
// ============================================================

bool Image::savePNG(std::string_view path) const {
    if (!valid()) return false;
    std::string p(path);
    int stride = static_cast<int>(rowBytes());
    int comps  = static_cast<int>(bytesPerPixel(m_format));
    return stbi_write_png(p.c_str(),
        static_cast<int>(m_width), static_cast<int>(m_height),
        comps, m_pixels.data(), stride) != 0;
}

bool Image::saveBMP(std::string_view path) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(m_format));
    return stbi_write_bmp(p.c_str(),
        static_cast<int>(m_width), static_cast<int>(m_height),
        comps, m_pixels.data()) != 0;
}

bool Image::saveTGA(std::string_view path) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(m_format));
    return stbi_write_tga(p.c_str(),
        static_cast<int>(m_width), static_cast<int>(m_height),
        comps, m_pixels.data()) != 0;
}

bool Image::saveJPG(std::string_view path, int quality) const {
    if (!valid()) return false;
    std::string p(path);
    int comps = static_cast<int>(bytesPerPixel(m_format));
    return stbi_write_jpg(p.c_str(),
        static_cast<int>(m_width), static_cast<int>(m_height),
        comps, m_pixels.data(), quality) != 0;
}

// ============================================================
// 工厂：从文件加载
// ============================================================

std::shared_ptr<Image> Image::load(std::string_view path) {
    return load(path, 0);
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

} // namespace MulanGeo::Core
