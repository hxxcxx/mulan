/**
 * @file Image.h
 * @brief 图像数据容器与 IO — CPU 端像素缓冲 + 文件加载/保存
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 设计思路：
 *  - Image 是不可变值对象，创建后 width/height/format 不可变
 *  - 通过工厂函数加载（返回 shared_ptr 便于缓存共享）
 *  - 支持离屏渲染回写：createFromBuffer() + saveToFile()
 *  - 所有像素操作通过 const 方法查询，非 const 通过 detachPixels 移出
 */

#pragma once

#include "../CoreExport.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::core {

// ============================================================
// 像素格式
// ============================================================

enum class PixelFormat : uint8_t {
    Unknown = 0,
    R8,          ///< 单通道 8-bit 灰度
    RG8,         ///< 双通道 8-bit
    RGB8,        ///< 三通道 8-bit
    RGBA8,       ///< 四通道 8-bit（推荐默认）
};

/// 每像素字节数
inline uint32_t bytesPerPixel(PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::R8:    return 1;
    case PixelFormat::RG8:   return 2;
    case PixelFormat::RGB8:  return 3;
    case PixelFormat::RGBA8: return 4;
    default:                 return 0;
    }
}

inline const char* pixelFormatName(PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::R8:    return "R8";
    case PixelFormat::RG8:   return "RG8";
    case PixelFormat::RGB8:  return "RGB8";
    case PixelFormat::RGBA8: return "RGBA8";
    default:                 return "Unknown";
    }
}

// ============================================================
// Image — 不可变图像数据
// ============================================================

class CORE_API Image {
public:
    // --- 构造（仅工厂函数可用）---

    Image() = default;

    /// 从已有像素缓冲构建（用于离屏渲染回写）
    Image(uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels);

    // --- 查询 ---

    uint32_t   width()    const { return m_width; }
    uint32_t   height()   const { return m_height; }
    PixelFormat format()  const { return m_format; }
    bool       valid()    const;

    uint32_t   rowBytes()      const { return m_width * bytesPerPixel(m_format); }
    size_t     totalBytes()    const { return m_pixels.size(); }

    /// 只读像素访问
    const uint8_t* data()         const { return m_pixels.data(); }
    const uint8_t* scanline(uint32_t row) const;

    // --- 变换 ---

    /// 垂直翻转（原地修改）
    void flipVertically();

    /// 转换到 RGBA8 格式（如已是 RGBA8 则返回自身的 shared_ptr）
    std::shared_ptr<Image> toRGBA() const;

    /// 移出像素数据（之后 Image 变为空）
    std::vector<uint8_t> detachPixels();

    // --- 保存 ---

    /// 保存为 PNG（自动根据 format 选择通道数）
    bool savePNG(std::string_view path) const;

    /// 保存为 BMP
    bool saveBMP(std::string_view path) const;

    /// 保存为 TGA
    bool saveTGA(std::string_view path) const;

    /// 保存为 JPG（quality 1-100）
    bool saveJPG(std::string_view path, int quality = 90) const;

    // --- 工厂：从文件加载 ---

    /// 从文件加载图像（自动检测格式，返回 nullptr 表示失败）
    static std::shared_ptr<Image> load(std::string_view path);

    /// 从文件加载，强制指定通道数
    static std::shared_ptr<Image> load(std::string_view path, int forceChannels);

    /// 从内存加载
    static std::shared_ptr<Image> loadFromMemory(const uint8_t* data, size_t size);

    /// 从内存加载，强制指定通道数
    static std::shared_ptr<Image> loadFromMemory(const uint8_t* data, size_t size, int forceChannels);

    // --- 工厂：从缓冲区构建 ---

    /// 从已有像素缓冲创建（用于离屏渲染回写）
    static std::shared_ptr<Image> createFromBuffer(
        uint32_t w, uint32_t h, PixelFormat fmt, std::vector<uint8_t> pixels);

    /// 创建空白图像
    static std::shared_ptr<Image> create(uint32_t w, uint32_t h, PixelFormat fmt);

    // --- 文件格式检测 ---

    /// 是否为支持的图像文件扩展名
    static bool isSupportedFile(std::string_view path);

private:
    uint32_t              m_width  = 0;
    uint32_t              m_height = 0;
    PixelFormat           m_format = PixelFormat::Unknown;
    std::vector<uint8_t>  m_pixels;
};

} // namespace mulan::core
