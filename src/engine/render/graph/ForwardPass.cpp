/**
 * @file ForwardPass.cpp
 * @brief 前向渲染 Pass 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "ForwardPass.h"
#include "ShaderUtil.h"
#include "../../rhi/BindGroup.h"
#include "../../rhi/RenderTypes.h"

#include <string>

namespace mulan::engine {

// ─── Scene UBO（对齐 solid.vert 的 sceneUBO）──────────────────

#pragma pack(push, 1)
struct alignas(16) SceneUniforms {
    float viewProj[16];
    float eyePos[4];
    float lightDir[4];
};
#pragma pack(pop)
static_assert(sizeof(SceneUniforms) == 96);

// ─── 构造 / init ───────────────────────────────────────────────

ForwardPass::ForwardPass(RHIDevice& device, GpuResourceManager& gpu,
                         const Camera& camera, const LightEnvironment& lightEnv)
    : m_device(device), m_gpu(gpu), m_camera(camera), m_lightEnv(lightEnv) {
}


bool ForwardPass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadSolidShaders()) return false;
    createSolidPSO(colorFmt, depthFmt, hasDepth);
    m_sceneUbo  = m_device.createBuffer(BufferDesc::uniform(256, "FwdSceneUBO"));
    m_objectUbo = m_device.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "FwdObjUBO"));   // 4096 objects
    m_initialized = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

bool ForwardPass::loadSolidShaders() {
    m_vs = loadShader(m_device, ShaderType::Vertex, "solid.vert");
    m_fs = loadShader(m_device, ShaderType::Pixel,  "solid.frag");
    return m_vs && m_fs;
}

// ─── PSO ───────────────────────────────────────────────────────

void ForwardPass::createSolidPSO(TextureFormat colorFmt, TextureFormat depthFmt,
                                  bool hasDepth) {
    VertexLayout vl;
    vl.begin()
      .add(VertexSemantic::Position,  VertexFormat::Float3)
      .add(VertexSemantic::Normal,    VertexFormat::Float3)
      .add(VertexSemantic::TexCoord0, VertexFormat::Float2);

    GraphicsPipelineDesc desc{};
    desc.name             = "ForwardSolid";
    desc.vs               = m_vs.get();
    desc.ps               = m_fs.get();
    desc.vertexLayout     = vl;
    desc.topology         = PrimitiveTopology::TriangleList;
    desc.cullMode         = CullMode::None;
    desc.frontFace        = FrontFace::CounterClockwise;
    desc.fillMode         = FillMode::Solid;
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = true;
    desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

    using PB = PipelineBinding;
    desc.descriptorBindings[0] = {
        .binding = 0, .count = 1,
        .type = DescriptorType::UniformBuffer,
        .stages = PB::kStageVertex | PB::kStageFragment};
    desc.descriptorBindings[1] = {
        .binding = 1, .count = 1,
        .type = DescriptorType::UniformBuffer,
        .stages = PB::kStageVertex};
    desc.descriptorBindingCount = 2;

    desc.colorFormats[0]    = colorFmt;
    desc.colorTargetCount   = 1;
    desc.depthStencilFormat = depthFmt;
    desc.depthEnable        = hasDepth;

    m_pso = m_device.createPipelineState(desc);
}

// ─── Execute ───────────────────────────────────────────────────

void ForwardPass::uploadSceneUBO(const PassContext& ctx) {
    Mat4 vp   = m_camera.projectionMatrix() * m_camera.viewMatrix();
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

void ForwardPass::execute(const PassContext& ctx) {
    if (!m_initialized || !m_pso || !ctx.cmd) return;

    uploadSceneUBO(ctx);

    for (auto& cmd : m_commands) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, m_sceneUbo.get(), m_objectUbo.get(), nullptr);
    }
}

} // namespace mulan::engine
