#include "geometry_draw_shared_resources.h"

#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../mesh_draw_command.h"
#include "../../rhi/device.h"

#include <mulan/core/log/log.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace mulan::engine {
namespace {

std::unique_ptr<Texture> createDefaultRGBA8Texture(RHIDevice& device, const char* name, const uint8_t rgba[4]) {
    TextureDesc texDesc;
    texDesc.name = name;
    texDesc.width = 1;
    texDesc.height = 1;
    texDesc.depth = 1;
    texDesc.format = TextureFormat::RGBA8_UNorm;
    texDesc.dimension = TextureDimension::Texture2D;
    texDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;

    auto result = device.createTexture(texDesc);
    if (!result) {
        return nullptr;
    }

    auto texture = std::move(*result);
    if (auto uploadResult = device.uploadTextureData(
                texture.get(),
                TextureUploadDesc::tightlyPacked(std::span(rgba, size_t{ 4 }), 1, 1, TextureFormat::RGBA8_UNorm));
        !uploadResult) {
        LOG_ERROR("[GeometryDrawSharedResources] Default texture upload failed: {}", uploadResult.error().message);
        return nullptr;
    }
    return texture;
}

std::unique_ptr<Texture> createDefaultEnvironmentIBLTexture(RHIDevice& device) {
    // 内置默认环境光照，保证未加载 HDR 时 PBR 仍有完整的环境明暗关系。
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

    TextureDesc texDesc;
    texDesc.name = "DefaultEnvironmentIBL";
    texDesc.format = TextureFormat::RGBA32_Float;
    texDesc.dimension = TextureDimension::Texture2D;
    texDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;

    auto result = device.createTexture(texDesc);
    if (!result) {
        return nullptr;
    }

    auto texture = std::move(*result);
    if (auto uploadResult = device.uploadTextureData(
                texture.get(),
                TextureUploadDesc::tightlyPacked(std::span(pixels), width, height, TextureFormat::RGBA32_Float));
        !uploadResult) {
        LOG_ERROR("[GeometryDrawSharedResources] Default environment upload failed: {}", uploadResult.error().message);
        return nullptr;
    }
    return texture;
}

}  // namespace

GeometryDrawSharedResources::GeometryDrawSharedResources(RHIDevice& device, MaterialCache& materialCache)
    : device_(device), material_cache_(materialCache) {
}

bool GeometryDrawSharedResources::init() {
    return createDefaultResources();
}

bool GeometryDrawSharedResources::createDefaultResources() {
    auto samplerResult = device_.createSampler(SamplerDesc::linear());
    if (!samplerResult) {
        LOG_ERROR("[GeometryDrawSharedResources] Default sampler creation failed");
        return false;
    }
    default_sampler_ = std::move(*samplerResult);

    const uint8_t white[4] = { 255, 255, 255, 255 };
    default_white_tex_ = createDefaultRGBA8Texture(device_, "DefaultWhite", white);
    if (!default_white_tex_) {
        LOG_ERROR("[GeometryDrawSharedResources] Default white texture creation failed");
        return false;
    }

    const uint8_t blackRGBA8[4] = { 0, 0, 0, 255 };
    default_black_tex_ = createDefaultRGBA8Texture(device_, "DefaultBlack", blackRGBA8);
    if (!default_black_tex_) {
        LOG_ERROR("[GeometryDrawSharedResources] Default black texture creation failed");
        return false;
    }

    const uint8_t normal[4] = { 128, 128, 255, 255 };
    default_normal_tex_ = createDefaultRGBA8Texture(device_, "DefaultNormal", normal);
    if (!default_normal_tex_) {
        LOG_ERROR("[GeometryDrawSharedResources] Default normal texture creation failed");
        return false;
    }

    const uint8_t metallicRoughness[4] = { 255, 255, 0, 255 };
    default_mr_tex_ = createDefaultRGBA8Texture(device_, "DefaultMetallicRoughness", metallicRoughness);
    if (!default_mr_tex_) {
        LOG_ERROR("[GeometryDrawSharedResources] Default metallic-roughness texture creation failed");
        return false;
    }

    default_ibl_tex_ = createDefaultEnvironmentIBLTexture(device_);
    if (!default_ibl_tex_) {
        LOG_ERROR("[GeometryDrawSharedResources] Default environment IBL texture creation failed");
        return false;
    }

    TextureDesc lutDesc;
    lutDesc.name = "DefaultBrdfLUT";
    lutDesc.format = TextureFormat::RG16_Float;
    lutDesc.dimension = TextureDimension::Texture2D;
    lutDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    lutDesc.width = 1;
    lutDesc.height = 1;
    lutDesc.depth = 1;
    auto lutResult = device_.createTexture(lutDesc);
    if (!lutResult) {
        LOG_ERROR("[GeometryDrawSharedResources] Default BRDF LUT creation failed");
        return false;
    }
    default_brdf_lut_ = std::move(*lutResult);
    const uint16_t lutData[2] = { 0x3C00, 0x0000 };
    if (auto uploadResult = device_.uploadTextureData(
                default_brdf_lut_.get(),
                TextureUploadDesc::tightlyPacked(std::span(lutData), 1, 1, TextureFormat::RG16_Float));
        !uploadResult) {
        LOG_ERROR("[GeometryDrawSharedResources] Default BRDF LUT upload failed: {}", uploadResult.error().message);
        default_brdf_lut_.reset();
        return false;
    }
    return true;
}

void GeometryDrawSharedResources::uploadFrameData(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv) {
    material_uniforms_.clear();
    scene_uniform_ = {};
    const auto result = ctx.cmd->writeUniform(buildSceneUniforms(ctx, lightEnv));
    if (!result) {
        LOG_ERROR("[GeometryDrawSharedResources] Scene uniform allocation failed: {}", result.error().message);
        return;
    }
    scene_uniform_ = *result;
}

std::optional<UniformSlice> GeometryDrawSharedResources::materialUniform(CommandList& commandList,
                                                                         uint32_t materialIndex) {
    if (const auto cached = material_uniforms_.find(materialIndex); cached != material_uniforms_.end())
        return cached->second;

    const Material* material = material_cache_.find(materialIndex);
    if (!material)
        material = material_cache_.find(0);
    if (!material)
        return std::nullopt;

    const MaterialGPU gpu = MaterialGPU::fromMaterial(*material);
    const auto result = commandList.writeUniform(gpu);
    if (!result)
        return std::nullopt;
    material_uniforms_.emplace(materialIndex, *result);
    return *result;
}

SceneUniforms GeometryDrawSharedResources::buildSceneUniforms(const DrawExecutionContext& ctx,
                                                              const LightEnvironment& lightEnv) const {
    math::Mat4 clip = device_.clipSpaceCorrectionMatrix();
    math::Mat4 view = ctx.camera.viewMatrix;
    math::Mat4 proj = ctx.camera.projectionMatrix;
    math::Mat4 vp = clip * proj * view;
    math::Vec3 eye = ctx.camera.eyePosition;

    const ResolvedLighting lighting = resolveLighting(lightEnv, view);
    const Light legacyLight = lighting.lightCount != 0
                                      ? lighting.lights[0]
                                      : Light::directional(math::Vec3(0.0, 0.0, -1.0), math::Vec3(0.0));

    SceneUniforms ubo{};
    storeGpuMat4(ubo.view, view);
    storeGpuMat4(ubo.projection, proj);
    storeGpuMat4(ubo.viewProjection, vp);
    storeGpuVec3(ubo.cameraPos, eye);
    // 保留旧单灯字段供 ViewCube 和旧着色路径消费；表面 Shader 使用下方 lights 数组。
    storeGpuVec3(ubo.lightDir, legacyLight.direction);
    storeGpuVec3(ubo.lightColor, legacyLight.color * legacyLight.intensity);
    storeGpuVec3(ubo.ambientColor, lighting.ambientColor);
    storeGpuVec3(ubo.edgeColor, math::Vec3(0.08, 0.08, 0.08));
    storeGpuVec3(ubo.highlightColor, math::Vec3(1.0, 0.5, 0.0));
    for (uint32_t index = 0; index < lighting.lightCount; ++index)
        ubo.lights[index] = makeSceneLightUniform(lighting.lights[index]);
    ubo.lightCount = lighting.lightCount;
    ubo.exposure = toFiniteGpuFloat(lighting.exposure);

    return ubo;
}

}  // namespace mulan::engine
