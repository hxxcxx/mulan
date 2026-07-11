#include "geometry_draw_shared_resources.h"

#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../mesh_draw_command.h"
#include "../../rhi/device.h"

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

    TextureDesc iblDesc;
    iblDesc.name = "DefaultIBL";
    iblDesc.format = TextureFormat::RGBA16_Float;
    iblDesc.dimension = TextureDimension::Texture2D;
    iblDesc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    iblDesc.width = 1;
    iblDesc.height = 1;
    iblDesc.depth = 1;
    auto iblResult = device_.createTexture(iblDesc);
    if (!iblResult) {
        std::fprintf(stderr, "[GeometryDrawSharedResources] create default IBL texture failed\n");
        return false;
    }
    default_ibl_tex_ = std::move(*iblResult);
    const float black[4] = { 0.f, 0.f, 0.f, 1.f };
    device_.uploadTextureData(default_ibl_tex_.get(),
                              TextureUploadDesc::tightlyPacked(std::span(black), 1, 1, TextureFormat::RGBA16_Float));

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
