#include "geometry_draw_shared_resources.h"

#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../mesh_draw_command.h"
#include "../../rhi/device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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
    device.uploadTextureData(texture.get(), TextureUploadDesc::tightlyPacked(std::span(rgba, size_t{ 4 }), 1, 1,
                                                                             TextureFormat::RGBA8_UNorm));
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
    device.uploadTextureData(texture.get(), TextureUploadDesc::tightlyPacked(std::span(pixels), width, height,
                                                                             TextureFormat::RGBA32_Float));
    return texture;
}

}  // namespace

GeometryDrawSharedResources::GeometryDrawSharedResources(RHIDevice& device, MaterialCache& materialCache,
                                                         const LightEnvironment& lightEnv)
    : device_(device), material_cache_(materialCache), light_env_(lightEnv) {
}

bool GeometryDrawSharedResources::init() {
    return createBuffers() && createDefaultResources();
}

bool GeometryDrawSharedResources::createBuffers() {
    auto sceneResult = device_.createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "SceneUBO"));
    if (!sceneResult) {
        return false;
    }
    scene_ubo_ = std::move(*sceneResult);

    auto objResult = device_.createBuffer(BufferDesc::uniform(MeshDrawCommand::kObjectUboBytes, "ObjUBO"));
    if (!objResult) {
        return false;
    }
    object_ubo_ = std::move(*objResult);

    auto matResult = device_.createBuffer(BufferDesc::uniform(MaterialCache::kMaxMaterials * 256, "MatUBO"));
    if (!matResult) {
        return false;
    }
    material_ubo_ = std::move(*matResult);
    return true;
}

bool GeometryDrawSharedResources::createDefaultResources() {
    auto samplerResult = device_.createSampler(SamplerDesc::linear());
    if (!samplerResult) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] createSampler failed\n");
        return false;
    }
    default_sampler_ = std::move(*samplerResult);

    const uint8_t white[4] = { 255, 255, 255, 255 };
    default_white_tex_ = createDefaultRGBA8Texture(device_, "DefaultWhite", white);
    if (!default_white_tex_) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default white texture failed\n");
        return false;
    }

    const uint8_t blackRGBA8[4] = { 0, 0, 0, 255 };
    default_black_tex_ = createDefaultRGBA8Texture(device_, "DefaultBlack", blackRGBA8);
    if (!default_black_tex_) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default black texture failed\n");
        return false;
    }

    const uint8_t normal[4] = { 128, 128, 255, 255 };
    default_normal_tex_ = createDefaultRGBA8Texture(device_, "DefaultNormal", normal);
    if (!default_normal_tex_) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default normal texture failed\n");
        return false;
    }

    const uint8_t metallicRoughness[4] = { 255, 255, 0, 255 };
    default_mr_tex_ = createDefaultRGBA8Texture(device_, "DefaultMetallicRoughness", metallicRoughness);
    if (!default_mr_tex_) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default metallic-roughness texture failed\n");
        return false;
    }

    default_ibl_tex_ = createDefaultEnvironmentIBLTexture(device_);
    if (!default_ibl_tex_) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default environment IBL texture failed\n");
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
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default brdf LUT failed\n");
        return false;
    }
    default_brdf_lut_ = std::move(*lutResult);
    const uint16_t lutData[2] = { 0x3C00, 0x0000 };
    device_.uploadTextureData(default_brdf_lut_.get(),
                              TextureUploadDesc::tightlyPacked(std::span(lutData), 1, 1, TextureFormat::RG16_Float));
    return true;
}

void GeometryDrawSharedResources::uploadFrameData(const DrawExecutionContext& ctx) {
    uploadSceneUBO(ctx);
    material_cache_.uploadDirtyMaterials(material_ubo_.get());
    material_cache_.clearDirtyMaterials();
}

void GeometryDrawSharedResources::uploadSceneUBO(const DrawExecutionContext& ctx) {
    math::Mat4 clip = device_.clipSpaceCorrectionMatrix();
    math::Mat4 view = ctx.camera.viewMatrix;
    math::Mat4 proj = ctx.camera.projectionMatrix;
    math::Mat4 vp = clip * proj * view;
    math::Vec3 eye = ctx.camera.eyePosition;

    auto* dl = light_env_.primaryDirectional();
    math::Vec3 ldir;
    math::Vec3 lightColor;
    if (dl) {
        // 模型/场景显式提供方向光时，忠实使用世界空间灯光。
        ldir = dl->direction.normalizedOr(math::Vec3(0.0, 0.0, -1.0));
        lightColor = dl->color * dl->intensity;
    } else {
        // 默认模型查看灯：相机空间左上前方的柔和方向光。它随观察方向旋转，
        // 类似系统 3D 查看器的棚拍主光，旋转模型时不会突然进入全黑背光面。
        const math::Vec3 viewLightDir = math::Vec3(0.35, -0.45, -0.82).normalized();
        ldir = (math::Mat3(view).transposed() * viewLightDir).normalizedOr(math::Vec3(0.0, 0.0, -1.0));
        lightColor = math::Vec3(0.95, 0.94, 0.92);
    }

    math::Vec3 ambientColor = light_env_.ambientColor * light_env_.ambientIntensity;
    if (ambientColor.lengthSq() <= 1.0e-12) {
        ambientColor = math::Vec3(0.35);
    }

    SceneUniforms ubo{};
    storeGpuMat4(ubo.view, view);
    storeGpuMat4(ubo.projection, proj);
    storeGpuMat4(ubo.viewProjection, vp);
    storeGpuVec3(ubo.cameraPos, eye);
    storeGpuVec3(ubo.lightDir, ldir);
    storeGpuVec3(ubo.lightColor, lightColor);
    storeGpuVec3(ubo.ambientColor, ambientColor);
    storeGpuVec3(ubo.edgeColor, math::Vec3(0.08, 0.08, 0.08));
    storeGpuVec3(ubo.highlightColor, math::Vec3(1.0, 0.5, 0.0));

    ctx.cmd->updateBuffer(scene_ubo_.get(), 0, sizeof(ubo), &ubo);
}

}  // namespace mulan::engine
