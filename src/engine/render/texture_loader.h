/**
 * @file texture_loader.h
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

#include "../rhi/texture.h"
#include <mulan/core/image/image.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// 加载选项
// ============================================================

struct TextureLoadOptions {
    bool generateMips = true;
    /// 是否使用 sRGB 纹理格式（硬件采样时自动 sRGB→linear，shader 无需手动转换）。
    /// 适用于 albedo/color 类贴图；normal/mr/ao 等数据贴图应设 false。
    /// 不设置时由 loadFromImage 按文件扩展名推断（.jpg/.jpeg → sRGB）。
    bool sRGB = false;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
};

// ============================================================
// 纹理加载结果 — RHI 友好的像素缓冲
// ============================================================

struct LoadedTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<uint8_t> pixels;
    TextureFormat format = TextureFormat::RGBA8_UNorm;
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
    LoadedTexture loadFromFile(const std::string& path, const TextureLoadOptions& options = {}) const;

    /// 从 core::Image 直接转换
    LoadedTexture loadFromImage(const std::shared_ptr<core::Image>& image,
                                const TextureLoadOptions& options = {}) const;

    /// 从内存加载
    LoadedTexture loadFromMemory(const uint8_t* data, size_t size, const TextureLoadOptions& options = {}) const;

    /// 检查文件是否为支持的图片格式
    static bool isSupportedFormat(const std::string& path);

private:
    /// core::PixelFormat + sRGB 意图 → RHI TextureFormat
    static TextureFormat toRHITextureFormat(core::PixelFormat pixelFmt, bool sRGB);

    /// 按文件扩展名推断是否应为 sRGB 格式（.jpg/.jpeg → true）
    static bool inferSrgb(const std::string& path);
};

}  // namespace mulan::engine
