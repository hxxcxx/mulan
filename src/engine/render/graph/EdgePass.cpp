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

#include <string>

namespace mulan::engine {

// ─── Scene UBO ─────────────────────────────────────────────────

#pragma pack(push, 1)
struct alignas(16) SceneUniforms {
    float viewProj[16];
    float eyePos[4];
    float lightDir[4];
};
#pragma pack(pop)
static_assert(sizeof(SceneUniforms) == 96);

// ─── 构造 / init ───────────────────────────────────────────────

EdgePass::EdgePass(RHIDevice& device, GpuResourceManager& gpu,
                   const Camera& camera, const LightEnvironment& lightEnv)
    : m_device(device), m_gpu(gpu), m_camera(camera), m_lightEnv(lightEnv) {
}

bool EdgePass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadEdgeShaders()) return false;
    createEdgePSO(colorFmt, depthFmt, hasDepth);
    m_sceneUbo  = m_device.createBuffer(BufferDesc::uniform(256, "EdgeSceneUBO"));
    m_objectUbo = m_device.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "EdgeObjUBO"));  // 4096 objects
    m_materialUbo = m_device.createBuffer(BufferDesc::uniform(256, "EdgeMatUBO"));
    // 填充默认材质（与 shader cbuffer Material 布局匹配，避免 descriptor 未更新）
    {
        struct DefaultMaterial {
            float baseColor[3];   float metallic;
            float emissive[3];    float roughness;
            float specular[3];    float shininess;
            float alpha;          float ao;
            float emissiveStrength; float alphaCutoff;
            uint32_t materialType; uint32_t alphaMode;
            uint32_t textureFlags;  uint32_t doubleSided;
        } dm{};
        dm.baseColor[0] = dm.baseColor[1] = dm.baseColor[2] = 0.83f;
        dm.alpha = 1.0f;
        dm.roughness = 0.5f;
        dm.doubleSided = 1;
        m_materialUbo->update(0, sizeof(dm), &dm);
    }
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
    Mat4 vp   = clip * m_camera.projectionMatrix() * m_camera.viewMatrix();
    Vec3 eye  = m_camera.eyePosition();
    auto* dl  = m_lightEnv.primaryDirectional();
    Vec3 ldir = dl ? glm::normalize(dl->direction) : Vec3(-0.3, -1.0, -0.4);

    SceneUniforms ubo{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            ubo.viewProj[c * 4 + r] = static_cast<float>(vp[c][r]);
    ubo.eyePos[0] = static_cast<float>(eye.x);
    ubo.eyePos[1] = static_cast<float>(eye.y);
    ubo.eyePos[2] = static_cast<float>(eye.z);
    ubo.eyePos[3] = 1.0f;
    ubo.lightDir[0] = static_cast<float>(ldir.x);
    ubo.lightDir[1] = static_cast<float>(ldir.y);
    ubo.lightDir[2] = static_cast<float>(ldir.z);
    ubo.lightDir[3] = 0.0f;

    ctx.cmd->updateBuffer(m_sceneUbo.get(), 0, sizeof(ubo), &ubo);
}

void EdgePass::execute(const PassContext& ctx) {
    if (!m_initialized || !m_pso || !ctx.cmd) return;

    uploadSceneUBO(ctx);

    for (auto& cmd : m_commands) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, m_sceneUbo.get(), m_objectUbo.get(), m_materialUbo.get());
    }
}

} // namespace mulan::engine
