/**
 * @file TextureLoader.h
 * @brief 纹理加载器 — 基于 core::Image 的 RHI 纹理桥接层
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 职责：
 *  - 将 core::Image (CPU 像素) 转换为 LoadedTexture (RHI 格式)
 *  - 提供 sRGB → Linear 转换
 *  - 提供 RHI TextureFormat 映射
 *
 * 实际图片加载由 core::Image::load() 完成
 */

#pragma once

#include "../RHI/Texture.h"
#include <mulan/Core/Image/Image.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// 加载选项
// ============================================================

struct TextureLoadOptions {
    bool           generateMips  = true;
    bool           srgbToLinear  = false;
    TextureFormat  format        = TextureFormat::RGBA8_UNorm;
};

// ============================================================
// 纹理加载结果 — RHI 友好的像素缓冲
// ============================================================

struct LoadedTexture {
    uint32_t             width    = 0;
    uint32_t             height   = 0;
    uint32_t             channels = 0;
    std::vector<uint8_t> pixels;
    TextureFormat        format   = TextureFormat::RGBA8_UNorm;
};

// ============================================================
// 纹理加载器 — 无状态工具类
// ============================================================

class TextureLoader {
public:
    TextureLoader() = default;
    ~TextureLoader() = default;

    TextureLoader(const TextureLoader&) = delete;
    TextureLoader& operator=(const TextureLoader&) = delete;

    /// 从文件加载（委托 core::Image::load + 转换）
    LoadedTexture loadFromFile(const std::string& path,
                               const TextureLoadOptions& options = {}) const;

    /// 从 core::Image 直接转换
    LoadedTexture loadFromImage(const std::shared_ptr<core::Image>& image,
                                const TextureLoadOptions& options = {}) const;

    /// 从内存加载
    LoadedTexture loadFromMemory(const uint8_t* data, size_t size,
                                 const TextureLoadOptions& options = {}) const;

    /// 检查文件是否为支持的图片格式
    static bool isSupportedFormat(const std::string& path);

    /// 从 PixelFormat + 文件扩展名 推测 RHI TextureFormat
    static TextureFormat guessFormat(const std::string& path, core::PixelFormat pixelFmt);

private:
    /// core::PixelFormat → RHI TextureFormat
    static TextureFormat toRHITextureFormat(core::PixelFormat pixelFmt, bool isSrgb);

    /// sRGB → Linear 就地转换
    static void convertSRGBToLinear(std::vector<uint8_t>& pixels);
};

} // namespace mulan::Engine
