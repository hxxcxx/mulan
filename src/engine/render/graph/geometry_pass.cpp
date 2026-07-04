#include "geometry_pass.h"
#include "shader_util.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/render_types.h"
#include "../../rhi/render_state.h"
#include "../material/material_cache.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

// ─── Scene UBO（严格对齐 Common.hlsli 的 cbuffer Scene，288 bytes）────────

#pragma pack(push, 1)
struct alignas(16) SceneUniforms {
    float view[16];           // offset 0
    float projection[16];     // offset 64
    float viewProjection[16]; // offset 128
    float cameraPos[4];       // offset 192 (xyz + pad)
    float lightDir[4];        // offset 208
    float lightColor[4];      // offset 224
    float ambientColor[4];    // offset 240
    float edgeColor[4];       // offset 256
    float highlightColor[4];  // offset 272
};
#pragma pack(pop)
static_assert(sizeof(SceneUniforms) == 288);

// ─── 构造 / init ───────────────────────────────────────────────

GeometryPass::GeometryPass(RHIDevice& device, RenderResourceCache& gpu,
                           MaterialCache& matCache,
                           const LightEnvironment& lightEnv,
                           GeometryPassConfig cfg)
    : device_(device), gpu_(gpu), mat_cache_(matCache), light_env_(lightEnv),
      cfg_(cfg) {
}


bool GeometryPass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadShaders()) return false;
    if (!createPSO(colorFmt, depthFmt, hasDepth)) return false;

    if (cfg_.sampleTextures) {
        if (!createDefaultResources()) return false;
    }

    auto sceneResult = device_.createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "SceneUBO"));
    if (!sceneResult) { return false; }
    scene_ubo_ = std::move(*sceneResult);

    auto objResult = device_.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "ObjUBO"));   // 4096 objects
    if (!objResult) { return false; }
    object_ubo_ = std::move(*objResult);

    auto matResult = device_.createBuffer(BufferDesc::uniform(
        MaterialCache::kMaxMaterials * 256, "MatUBO"));  // MaterialCache统一尺寸
    if (!matResult) { return false; }
    material_ubo_ = std::move(*matResult);

    initialized_ = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

bool GeometryPass::loadShaders() {
    // 按 cfg_.shaderBase 拼 "<base>.vert" / "<base>.frag"
    const std::string vertName = std::string(cfg_.shaderBase) + ".vert";
    const std::string fragName = std::string(cfg_.shaderBase) + ".frag";

    auto vs = loadShader(device_, ShaderType::Vertex, vertName.c_str());
    auto fs = loadShader(device_, ShaderType::Pixel,  fragName.c_str());
    if (!vs) { return false; }
    if (!fs) { return false; }
    vs_ = std::move(*vs);
    fs_ = std::move(*fs);
    return true;
}

// ─── PSO ───────────────────────────────────────────────────────

bool GeometryPass::createPSO(TextureFormat colorFmt, TextureFormat depthFmt,
                              bool hasDepth) {
    VertexLayout vl = layouts::surface();

    GraphicsPipelineDesc desc{};
    desc.name             = cfg_.passName;
    desc.vs               = vs_.get();
    desc.ps               = fs_.get();
    desc.vertexLayout     = vl;
    desc.topology         = cfg_.topology;
    desc.cullMode         = CullMode::None;
    desc.frontFace        = FrontFace::CounterClockwise;
    desc.fillMode         = FillMode::Solid;
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = cfg_.depthWrite;
    desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

    using PB = PipelineBinding;
    desc.descriptorBindings[0] = {
        .binding = 0, .count = 1,
        .type = DescriptorType::UniformBuffer,
        .stages = PB::kStageVertex | PB::kStageFragment};
    desc.descriptorBindings[1] = {
        .binding = 1, .count = 1,
        .type = DescriptorType::UniformBuffer,
        .stages = PB::kStageVertex | PB::kStageFragment};
    desc.descriptorBindings[2] = {
        .binding = 2, .count = 1,
        .type = DescriptorType::UniformBuffer,
        .stages = PB::kStageFragment};
    uint8_t bindingCount = 3;

    // 纹理 + sampler（仅 sampleTextures=true 的 pass 声明）
    if (cfg_.sampleTextures) {
        desc.descriptorBindings[bindingCount++] = {
            .binding = 3, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 4, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 5, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 6, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 7, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 8, .count = 1,
            .type = DescriptorType::Sampler,
            .stages = PB::kStageFragment};
    }
    desc.descriptorBindingCount = bindingCount;

    desc.colorFormats[0]    = colorFmt;
    desc.colorTargetCount   = 1;
    desc.depthStencilFormat = depthFmt;
    desc.depthEnable        = hasDepth;

    auto psoResult = device_.createPipelineState(desc);
    if (!psoResult) { return false; }
    pso_ = std::move(*psoResult);
    return true;
}

// ─── Execute ───────────────────────────────────────────────────

bool GeometryPass::createDefaultResources() {
    // 默认线性 sampler（Linear 过滤 + Repeat 寻址）
    auto samplerResult = device_.createSampler(SamplerDesc::linear());
    if (!samplerResult) {
        std::fprintf(stderr, "[GeometryPass] createSampler failed\n");
        return false;
    }
    default_sampler_ = std::move(*samplerResult);

    // 1×1 RGBA(255,255,255,255) 白纹理 — 无材质模型退化用。本 pass 独占所有权。
    // usage 加 GenerateMips：该 flag 在 VK 后端映射为 eTransferSrc|eTransferDst，
    // uploadTextureData 的 vkCmdCopyBufferToImage 需要 eTransferDst（见 vk_texture.cpp:39）。
    TextureDesc texDesc;
    texDesc.width     = 1;
    texDesc.height    = 1;
    texDesc.depth     = 1;
    texDesc.format    = TextureFormat::RGBA8_UNorm;
    texDesc.dimension = TextureDimension::Texture2D;
    texDesc.usage     = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
    auto texResult = device_.createTexture(texDesc);
    if (!texResult) {
        std::fprintf(stderr, "[GeometryPass] create default white texture failed\n");
        return false;
    }
    default_white_tex_ = std::move(*texResult);
    const uint8_t white[4] = {255, 255, 255, 255};
    device_.uploadTextureData(default_white_tex_.get(), white, 1, 1, TextureFormat::RGBA8_UNorm);
    return true;
}

void GeometryPass::uploadSceneUBO(const PassContext& ctx) {
    // 应用 Vulkan clip space 修正（Z:[-1,1]→[0,1], Y 翻转）
    math::Mat4 clip = device_.clipSpaceCorrectionMatrix();
    math::Mat4 view = ctx.camera.viewMatrix;
    math::Mat4 proj = ctx.camera.projectionMatrix;
    math::Mat4 vp   = clip * proj * view;
    math::Vec3 eye  = ctx.camera.eyePosition;
    auto* dl  = light_env_.primaryDirectional();
    math::Vec3 ldir = dl ? dl->direction.normalized() : math::Vec3(-0.3, -1.0, -0.4);

    auto storeMat = [](float* dst, const math::Mat4& m) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                dst[c * 4 + r] = static_cast<float>(m[c][r]);
    };
    auto storeVec3 = [](float* dst, const math::Vec3& v) {
        dst[0] = static_cast<float>(v.x);
        dst[1] = static_cast<float>(v.y);
        dst[2] = static_cast<float>(v.z);
        dst[3] = 0.0f;
    };

    SceneUniforms ubo{};
    storeMat(ubo.view,           view);
    storeMat(ubo.projection,     proj);
    storeMat(ubo.viewProjection, vp);
    storeVec3(ubo.cameraPos,   eye);
    storeVec3(ubo.lightDir,    ldir);
    storeVec3(ubo.lightColor,  math::Vec3(0.8));    // 主光稍弱
    storeVec3(ubo.ambientColor,math::Vec3(0.15));   // 环境光弱，避免 ×3.5 后过曝
    storeVec3(ubo.edgeColor,   math::Vec3(0.08, 0.08, 0.08));
    storeVec3(ubo.highlightColor, math::Vec3(1.0, 0.5, 0.0));

    ctx.cmd->updateBuffer(scene_ubo_.get(), 0, sizeof(ubo), &ubo);
}

void GeometryPass::execute(const PassContext& ctx) {
    if (!initialized_ || !pso_ || !ctx.cmd) return;

    uploadSceneUBO(ctx);
    mat_cache_.uploadDirtyMaterials(material_ubo_.get());

    for (auto& cmd : commands_) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, scene_ubo_.get(), object_ubo_.get(),
                    material_ubo_.get(), default_white_tex_.get(), default_sampler_.get());
    }
}

} // namespace mulan::engine
