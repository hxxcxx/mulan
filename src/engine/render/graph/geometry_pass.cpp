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

    if (!createFrameBindGroup(colorFmt, depthFmt, hasDepth)) return false;

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
        // IBL 三件套：binding 9=irradiance, 10=prefilter, 11=brdf LUT
        desc.descriptorBindings[bindingCount++] = {
            .binding = 9, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 10, .count = 1,
            .type = DescriptorType::TextureSRV,
            .stages = PB::kStageFragment};
        desc.descriptorBindings[bindingCount++] = {
            .binding = 11, .count = 1,
            .type = DescriptorType::TextureSRV,
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

    // IBL fallback：1×1 RGBA16F 黑色 2D 纹理（无 IBL 时漫反射/镜面反射=0）
    if (cfg_.sampleTextures) {
        TextureDesc iblDesc;
        iblDesc.name      = "DefaultIBL";
        iblDesc.format    = TextureFormat::RGBA16_Float;
        iblDesc.dimension = TextureDimension::Texture2D;
        iblDesc.usage     = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
        iblDesc.width     = 1;
        iblDesc.height    = 1;
        iblDesc.depth     = 1;
        auto iblR = device_.createTexture(iblDesc);
        if (!iblR) {
            std::fprintf(stderr, "[GeometryPass] create default IBL texture failed\n");
            return false;
        }
        default_ibl_tex_ = std::move(*iblR);
        const float black[4] = {0.f, 0.f, 0.f, 1.f};
        device_.uploadTextureData(default_ibl_tex_.get(), black, 1, 1,
                                  TextureFormat::RGBA16_Float);

        // BRDF LUT fallback：1×1 RG16F，r=1, g=0（让 specular 至少反射环境本身）
        TextureDesc lutDesc;
        lutDesc.name      = "DefaultBrdfLUT";
        lutDesc.format    = TextureFormat::RG16_Float;
        lutDesc.dimension = TextureDimension::Texture2D;
        lutDesc.usage     = TextureUsageFlags::ShaderResource | TextureUsageFlags::GenerateMips;
        lutDesc.width     = 1;
        lutDesc.height    = 1;
        lutDesc.depth     = 1;
        auto lutR = device_.createTexture(lutDesc);
        if (!lutR) {
            std::fprintf(stderr, "[GeometryPass] create default brdf LUT failed\n");
            return false;
        }
        default_brdf_lut_ = std::move(*lutR);
        // RG16F：half r=1.0 (0x3C00), half g=0.0 (0x0000)，4 字节
        const uint16_t lutData[2] = {0x3C00, 0x0000};
        device_.uploadTextureData(default_brdf_lut_.get(), lutData, 1, 1,
                                  TextureFormat::RG16_Float);
    }
    return true;
}

bool GeometryPass::createFrameBindGroup(TextureFormat, TextureFormat, bool) {
    // 构建初始 BindGroupDesc：静态 binding（scene=0）在此绑定；
    // object=1 / material=2 offset 在每 draw 通过 updateUBO 刷新（首帧必脏）；
    // 纹理槽先用 defaultWhite 占位，每 draw 由 MeshDrawCommand::execute 更新。
    BindGroupDesc bg;
    bg.addUBO(0, scene_ubo_.get(), 0, sizeof(SceneUniforms));
    bg.addUBO(1, object_ubo_.get(), 0, MeshDrawCommand::kObjectUboStride);
    bg.addUBO(2, material_ubo_.get(), 0, 128);

    if (cfg_.sampleTextures && default_white_tex_ && default_sampler_) {
        bg.addTexture(3, default_white_tex_.get());
        bg.addTexture(4, default_white_tex_.get());
        bg.addTexture(5, default_white_tex_.get());
        bg.addTexture(6, default_white_tex_.get());
        bg.addTexture(7, default_white_tex_.get());
        bg.addSampler(8, default_sampler_.get());
        // IBL 三件套：先用 fallback，每帧 execute 时刷新为真实烘焙产物。
        // 若 IBL fallback 未创建（创建失败），退化到 defaultWhite 以保证 descriptor 非 null
        // —— 避免 Vulkan 验证层 "descriptor never updated" 错误。
        Texture* iblFallback = default_ibl_tex_ ? default_ibl_tex_.get() : default_white_tex_.get();
        Texture* lutFallback = default_brdf_lut_ ? default_brdf_lut_.get() : default_white_tex_.get();
        bg.addTexture(9,  iblFallback);
        bg.addTexture(10, iblFallback);
        bg.addTexture(11, lutFallback);
    }

    auto result = device_.createBindGroup(pso_->bindGroupLayout(), bg);
    if (!result) {
        std::fprintf(stderr, "[GeometryPass] createBindGroup failed: %s\n",
                     result.error().message.c_str());
        return false;
    }
    frame_bg_ = std::move(*result);
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
    if (!initialized_ || !pso_ || !ctx.cmd || !frame_bg_) return;

    uploadSceneUBO(ctx);
    mat_cache_.uploadDirtyMaterials(material_ubo_.get());

    // binding=0 (scene UBO) 内容每帧由 uploadSceneUBO 更新，但 binding 本身指向的
    // buffer/offset/range 不变 → 无需 update，复用缓存 descriptor。
    // binding=9/10/11 (IBL 三件套) 在 setIBLTextures 后生效，每帧刷新一次。
    if (cfg_.sampleTextures) {
        frame_bg_->updateTexture(9,  ibl_irradiance_ ? ibl_irradiance_ : default_ibl_tex_.get());
        frame_bg_->updateTexture(10, ibl_prefilter_  ? ibl_prefilter_  : default_ibl_tex_.get());
        frame_bg_->updateTexture(11, ibl_brdf_lut_   ? ibl_brdf_lut_   : default_brdf_lut_.get());
    }

    for (auto& cmd : commands_) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, *frame_bg_, scene_ubo_.get(), object_ubo_.get(),
                    material_ubo_.get(), default_white_tex_.get(),
                    default_sampler_.get());
    }
}

} // namespace mulan::engine
