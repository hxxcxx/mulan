/**
 * @file texture.h
 * @brief 纹理资源描述与接口定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace mulan::engine {

// ============================================================
// 纹理格式
// ============================================================

enum class TextureFormat : uint8_t {
    Unknown = 0,

    // --- 8-bit UNORM ---
    RGBA8_UNorm,
    BGRA8_UNorm,
    R8_UNorm,

    // --- 8-bit sRGB ---
    RGBA8_sRGB,
    BGRA8_sRGB,

    // --- 16-bit float ---
    RGBA16_Float,
    R16_Float,

    // --- 32-bit float ---
    RGBA32_Float,
    R32_Float,
    RG32_Float,

    // --- 深度 ---
    D16_UNorm,
    D24_UNorm_S8_UInt,
    D32_Float,
    D32_Float_S8X24_UInt,

    // --- 压缩 ---
    BC1_RGBA_UNorm,    // DXT1
    BC3_RGBA_UNorm,    // DXT5
    BC5_RG_UNorm,      // 法线贴图
    BC7_RGBA_UNorm,
    BC7_RGBA_sRGB,
};

// ============================================================
// 纹理维度
// ============================================================

enum class TextureDimension : uint8_t {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
};

// ============================================================
// 纹理用途（位掩码）
// ============================================================

enum class TextureUsageFlags : uint8_t {
    None           = 0,
    ShaderResource = 1 << 0,  // SRV / sampler
    RenderTarget   = 1 << 1,  // color RTV
    DepthStencil   = 1 << 2,  // DSV
    UnorderedAccess = 1 << 3, // UAV
    GenerateMips   = 1 << 4,
};

constexpr TextureUsageFlags operator|(TextureUsageFlags a, TextureUsageFlags b) {
    return static_cast<TextureUsageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool operator&(TextureUsageFlags a, TextureUsageFlags b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================
// 纹理格式工具
// ============================================================

/// 返回每像素字节数（上传/staging 布局计算用）。未知格式返回 0。
/// 仅覆盖可被 CPU 上传的颜色格式；深度/压缩格式返回 0。
constexpr uint32_t textureFormatBytesPerPixel(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::R8_UNorm:           return 1;
    case TextureFormat::RGBA8_UNorm:
    case TextureFormat::BGRA8_UNorm:
    case TextureFormat::RGBA8_sRGB:
    case TextureFormat::BGRA8_sRGB:         return 4;
    case TextureFormat::RGBA16_Float:       return 8;
    case TextureFormat::R16_Float:          return 2;
    case TextureFormat::RGBA32_Float:       return 16;
    case TextureFormat::R32_Float:          return 4;
    case TextureFormat::RG32_Float:         return 8;
    default:                                return 0;  // 深度/压缩不支持 CPU 直接上传
    }
}

// ============================================================
// 纹理描述结构体
// ============================================================

struct TextureDesc {
    std::string_view name;
    TextureFormat    format     = TextureFormat::RGBA8_UNorm;
    TextureDimension dimension  = TextureDimension::Texture2D;
    TextureUsageFlags usage     = TextureUsageFlags::ShaderResource;
    uint32_t         width      = 0;
    uint32_t         height     = 0;
    uint32_t         depth      = 1;
    uint32_t         mipLevels  = 1;
    uint32_t         arraySize  = 1;
    uint32_t         sampleCount = 1;

    // 便捷构造

    static TextureDesc renderTarget(uint32_t w, uint32_t h,
                                    TextureFormat fmt = TextureFormat::RGBA8_UNorm,
                                    std::string_view debugName = {}) {
        return {debugName, fmt, TextureDimension::Texture2D,
                TextureUsageFlags::RenderTarget | TextureUsageFlags::ShaderResource,
                w, h, 1, 1, 1, 1};
    }

    static TextureDesc depthStencil(uint32_t w, uint32_t h,
                                    TextureFormat fmt = TextureFormat::D24_UNorm_S8_UInt,
                                    std::string_view debugName = {}) {
        return {debugName, fmt, TextureDimension::Texture2D,
                TextureUsageFlags::DepthStencil | TextureUsageFlags::ShaderResource,
                w, h, 1, 1, 1, 1};
    }
};

// ============================================================
// 纹理基类
// ============================================================

class Texture {
public:
    virtual ~Texture() = default;

    virtual const TextureDesc& desc() const = 0;

    uint32_t width()  const { return desc().width; }
    uint32_t height() const { return desc().height; }
    TextureFormat format() const { return desc().format; }

protected:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
};

} // namespace mulan::engine
