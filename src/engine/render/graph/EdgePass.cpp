/**
 * @file EdgePass.cpp
 * @brief 边线渲染 Pass 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "EdgePass.h"
#include "ShaderUtil.h"
#include "../../rhi/BindGroup.h"
#include "../../rhi/RenderTypes.h"
#include "../material/MaterialCache.h"

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
    : m_device(device), m_gpu(gpu), m_matCache(matCache), m_camera(camera), m_lightEnv(lightEnv) {
}

bool EdgePass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadEdgeShaders()) return false;
    createEdgePSO(colorFmt, depthFmt, hasDepth);
    m_sceneUbo  = m_device.createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "EdgeSceneUBO"));
    m_objectUbo = m_device.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "EdgeObjUBO"));  // 4096 objects
    m_materialUbo = m_device.createBuffer(BufferDesc::uniform(
        MaterialCache::kMaxMaterials * 256, "EdgeMatUBO"));

    m_initialized = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

bool EdgePass::loadEdgeShaders() {
    m_vs = loadShader(m_device, ShaderType::Vertex, "edge.vert");
    m_fs = loadShader(m_device, ShaderType::Pixel,  "edge.frag");
    return m_vs && m_fs;
}

// ─── PSO ───────────────────────────────────────────────────────

void EdgePass::createEdgePSO(TextureFormat colorFmt, TextureFormat depthFmt,
                              bool hasDepth) {
    VertexLayout vl;
    vl.begin()
      .add(VertexSemantic::Position,  VertexFormat::Float3)
      .add(VertexSemantic::Normal,    VertexFormat::Float3)
      .add(VertexSemantic::TexCoord0, VertexFormat::Float2);

    GraphicsPipelineDesc desc{};
    desc.name             = "EdgeSolid";
    desc.vs               = m_vs.get();
    desc.ps               = m_fs.get();
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

    m_pso = m_device.createPipelineState(desc);
}

// ─── Execute ───────────────────────────────────────────────────

void EdgePass::uploadSceneUBO(const PassContext& ctx) {
    // 应用 Vulkan clip space 修正（Z:[-1,1]→[0,1], Y 翻转）
    Mat4 clip = m_device.clipSpaceCorrectionMatrix();
    Mat4 view = m_camera.viewMatrix();
    Mat4 proj = m_camera.projectionMatrix();
    Mat4 vp   = clip * proj * view;
    Vec3 eye  = m_camera.eyePosition();
    auto* dl  = m_lightEnv.primaryDirectional();
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

    ctx.cmd->updateBuffer(m_sceneUbo.get(), 0, sizeof(ubo), &ubo);
}

void EdgePass::execute(const PassContext& ctx) {
    if (!m_initialized || !m_pso || !ctx.cmd) return;

    uploadSceneUBO(ctx);
    m_matCache.uploadDirtyMaterials(m_materialUbo.get());

    for (auto& cmd : m_commands) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, m_sceneUbo.get(), m_objectUbo.get(), m_materialUbo.get());
    }
}

} // namespace mulan::engine
