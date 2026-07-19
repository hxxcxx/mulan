/**
 * @file draw_fallback_resources.cpp
 * @brief Device 级绘制兜底资源的事务式创建实现。
 * @author hxxcxx
 * @date 2026-07-18
 */

#include "draw_fallback_resources.h"
#include "../../rhi/device.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace mulan::engine {
namespace {

Result<std::unique_ptr<Texture>> createRgba8Texture(RHIDevice& device, const char* name,
                                                    const std::array<uint8_t, 4>& rgba) {
    TextureDesc desc;
    desc.name = name;
    desc.width = 1;
    desc.height = 1;
    desc.depth = 1;
    desc.format = TextureFormat::RGBA8_UNorm;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;

    auto created = device.createTexture(desc);
    if (!created) {
        return std::unexpected(created.error());
    }
    auto texture = std::move(*created);
    if (auto uploaded = device.uploadTextureData(
                texture.get(), TextureUploadDesc::tightlyPacked(std::span(rgba), 1, 1, TextureFormat::RGBA8_UNorm));
        !uploaded) {
        return std::unexpected(uploaded.error());
    }
    return texture;
}

Result<std::unique_ptr<Texture>> createEnvironmentTexture(RHIDevice& device) {
    // 内置环境只承担稳定退化路径；尺寸刻意保持很小，真实 IBL 就绪后会替换该 binding。
    constexpr uint32_t width = 16;
    constexpr uint32_t height = 8;
    std::array<float, width * height * 4> pixels{};

    for (uint32_t y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float elevation = 1.0f - v;
        const float skyBlend = std::clamp((elevation + 0.15f) / 0.75f, 0.0f, 1.0f);

        const std::array<float, 3> horizon = { 0.56f, 0.59f, 0.64f };
        const std::array<float, 3> sky = { 0.78f, 0.84f, 0.94f };
        const std::array<float, 3> ground = { 0.26f, 0.23f, 0.21f };
        std::array<float, 3> rowColor{};
        if (elevation >= -0.15f) {
            for (int c = 0; c < 3; ++c) {
                rowColor[c] = horizon[c] + (sky[c] - horizon[c]) * skyBlend;
            }
        } else {
            const float groundBlend = std::clamp((-elevation - 0.15f) / 0.85f, 0.0f, 1.0f);
            for (int c = 0; c < 3; ++c) {
                rowColor[c] = horizon[c] + (ground[c] - horizon[c]) * groundBlend;
            }
        }

        for (uint32_t x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float keyDistance = std::abs(u - 0.22f);
            const float key = std::clamp(1.0f - keyDistance / 0.16f, 0.0f, 1.0f) *
                              std::clamp(1.0f - std::abs(elevation - 0.42f) / 0.28f, 0.0f, 1.0f);
            const float rimDistance = std::abs(u - 0.72f);
            const float rim = std::clamp(1.0f - rimDistance / 0.12f, 0.0f, 1.0f) *
                              std::clamp(1.0f - std::abs(elevation - 0.25f) / 0.32f, 0.0f, 1.0f);

            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            pixels[offset + 0] = rowColor[0] + key * 1.05f + rim * 0.30f;
            pixels[offset + 1] = rowColor[1] + key * 1.00f + rim * 0.34f;
            pixels[offset + 2] = rowColor[2] + key * 0.92f + rim * 0.42f;
            pixels[offset + 3] = 1.0f;
        }
    }

    TextureDesc desc;
    desc.name = "DefaultEnvironmentIBL";
    desc.format = TextureFormat::RGBA32_Float;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;

    auto created = device.createTexture(desc);
    if (!created) {
        return std::unexpected(created.error());
    }
    auto texture = std::move(*created);
    if (auto uploaded = device.uploadTextureData(
                texture.get(),
                TextureUploadDesc::tightlyPacked(std::span(pixels), width, height, TextureFormat::RGBA32_Float));
        !uploaded) {
        return std::unexpected(uploaded.error());
    }
    return texture;
}

Result<std::unique_ptr<Texture>> createBrdfLutTexture(RHIDevice& device) {
    TextureDesc desc;
    desc.name = "DefaultBrdfLUT";
    desc.format = TextureFormat::RG16_Float;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    desc.width = 1;
    desc.height = 1;
    desc.depth = 1;

    auto created = device.createTexture(desc);
    if (!created) {
        return std::unexpected(created.error());
    }
    auto texture = std::move(*created);
    const std::array<uint16_t, 2> pixels = { 0x3C00, 0x0000 };
    if (auto uploaded = device.uploadTextureData(
                texture.get(), TextureUploadDesc::tightlyPacked(std::span(pixels), 1, 1, TextureFormat::RG16_Float));
        !uploaded) {
        return std::unexpected(uploaded.error());
    }
    return texture;
}

}  // namespace

DrawFallbackResources::DrawFallbackResources(RHIDevice& device) : device_(device) {
}

ResultVoid DrawFallbackResources::init() {
    MULAN_PROFILE_ZONE();

    if (initialized_) {
        return {};
    }

    // 资源一经写入上传批次，就必须存活到调用方 flush。成员在初始化成功前不会通过
    // 访问器发布，但失败时仍负责保活已录制的目标资源，随后由候选对象统一回滚。
    sampler_.reset();
    white_texture_.reset();
    black_texture_.reset();
    normal_texture_.reset();
    metallic_roughness_texture_.reset();
    environment_texture_.reset();
    brdf_lut_texture_.reset();

    auto sampler = device_.createSampler(SamplerDesc::linear());
    if (!sampler) {
        return std::unexpected(sampler.error());
    }
    sampler_ = std::move(*sampler);
    auto white = createRgba8Texture(device_, "DefaultWhite", { 255, 255, 255, 255 });
    if (!white) {
        return std::unexpected(white.error());
    }
    white_texture_ = std::move(*white);
    auto black = createRgba8Texture(device_, "DefaultBlack", { 0, 0, 0, 255 });
    if (!black) {
        return std::unexpected(black.error());
    }
    black_texture_ = std::move(*black);
    auto normal = createRgba8Texture(device_, "DefaultNormal", { 128, 128, 255, 255 });
    if (!normal) {
        return std::unexpected(normal.error());
    }
    normal_texture_ = std::move(*normal);
    auto metallicRoughness = createRgba8Texture(device_, "DefaultMetallicRoughness", { 255, 255, 0, 255 });
    if (!metallicRoughness) {
        return std::unexpected(metallicRoughness.error());
    }
    metallic_roughness_texture_ = std::move(*metallicRoughness);
    auto environment = createEnvironmentTexture(device_);
    if (!environment) {
        return std::unexpected(environment.error());
    }
    environment_texture_ = std::move(*environment);
    auto brdfLut = createBrdfLutTexture(device_);
    if (!brdfLut) {
        return std::unexpected(brdfLut.error());
    }
    brdf_lut_texture_ = std::move(*brdfLut);
    initialized_ = true;
    return {};
}

}  // namespace mulan::engine
