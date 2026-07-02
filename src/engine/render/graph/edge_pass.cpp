#include "edge_pass.h"
#include "shader_util.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/render_types.h"
#include "../material/material_cache.h"

#include <mulan/core/log/log.h>

#include <string>

namespace mulan::engine {

// ─── Scene UBO（严格对齐 Common.hlsli 的 cbuffer Scene，288 bytes）────────

#pragma pack(push, 1)
struct alignas(16) SceneUniforms {
    float view[16];
    float projection[16];
    float viewProjection[16];
    float cameraPos[4];
    float lightDir[4];
    float lightColor[4];
    float ambientColor[4];
    float edgeColor[4];
    float highlightColor[4];
};
#pragma pack(pop)
static_assert(sizeof(SceneUniforms) == 288);

// ─── 构造 / init ───────────────────────────────────────────────

EdgePass::EdgePass(RHIDevice& device, GpuResourceManager& gpu,
                   MaterialCache& matCache,
                   const Camera& camera, const LightEnvironment& lightEnv)
    : device_(device), gpu_(gpu), mat_cache_(matCache), camera_(camera), light_env_(lightEnv) {
}

bool EdgePass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadEdgeShaders()) return false;
    if (!createEdgePSO(colorFmt, depthFmt, hasDepth)) return false;

    auto sceneResult = device_.createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "EdgeSceneUBO"));
    if (!sceneResult) { return false; }
    scene_ubo_ = std::move(*sceneResult);

    auto objResult = device_.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "EdgeObjUBO"));  // 4096 objects
    if (!objResult) { return false; }
    object_ubo_ = std::move(*objResult);

    auto matResult = device_.createBuffer(BufferDesc::uniform(
        MaterialCache::kMaxMaterials * 256, "EdgeMatUBO"));
    if (!matResult) { return false; }
    material_ubo_ = std::move(*matResult);

    initialized_ = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

bool EdgePass::loadEdgeShaders() {
    auto vs = loadShader(device_, ShaderType::Vertex, "edge.vert");
    auto fs = loadShader(device_, ShaderType::Pixel,  "edge.frag");
    if (!vs) { return false; }
    if (!fs) { return false; }
    vs_ = std::move(*vs);
    fs_ = std::move(*fs);
    return true;
}

// ─── PSO ───────────────────────────────────────────────────────

bool EdgePass::createEdgePSO(TextureFormat colorFmt, TextureFormat depthFmt,
                              bool hasDepth) {
    VertexLayout vl = layouts::cadSolid();

    GraphicsPipelineDesc desc{};
    desc.name             = "EdgeSolid";
    desc.vs               = vs_.get();
    desc.ps               = fs_.get();
    desc.vertexLayout     = vl;
    desc.topology         = PrimitiveTopology::LineList;
    desc.cullMode         = CullMode::None;
    desc.frontFace        = FrontFace::CounterClockwise;
    desc.fillMode         = FillMode::Solid;
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
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
    desc.descriptorBindingCount = 3;

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

void EdgePass::uploadSceneUBO(const PassContext& ctx) {
    // 应用 Vulkan clip space 修正（Z:[-1,1]→[0,1], Y 翻转）
    Mat4 clip = device_.clipSpaceCorrectionMatrix();
    Mat4 view = camera_.viewMatrix();
    Mat4 proj = camera_.projectionMatrix();
    Mat4 vp   = clip * proj * view;
    Vec3 eye  = camera_.eyePosition();
    auto* dl  = light_env_.primaryDirectional();
    Vec3 ldir = dl ? glm::normalize(dl->direction) : Vec3(-0.3, -1.0, -0.4);

    auto storeMat = [](float* dst, const Mat4& m) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                dst[c * 4 + r] = static_cast<float>(m[c][r]);
    };
    auto storeVec3 = [](float* dst, const Vec3& v) {
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
    storeVec3(ubo.lightColor,  Vec3(0.8));
    storeVec3(ubo.ambientColor,Vec3(0.15));
    storeVec3(ubo.edgeColor,   Vec3(0.08, 0.08, 0.08));
    storeVec3(ubo.highlightColor, Vec3(1.0, 0.5, 0.0));

    ctx.cmd->updateBuffer(scene_ubo_.get(), 0, sizeof(ubo), &ubo);
}

void EdgePass::execute(const PassContext& ctx) {
    if (!initialized_ || !pso_ || !ctx.cmd) return;

    uploadSceneUBO(ctx);
    mat_cache_.uploadDirtyMaterials(material_ubo_.get());

    for (auto& cmd : commands_) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, scene_ubo_.get(), object_ubo_.get(), material_ubo_.get());
    }
}

} // namespace mulan::engine
