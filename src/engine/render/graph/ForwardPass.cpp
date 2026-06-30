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

ForwardPass::ForwardPass(RHIDevice& device, GpuResourceManager& gpu,
                         const Camera& camera, const LightEnvironment& lightEnv)
    : m_device(device), m_gpu(gpu), m_camera(camera), m_lightEnv(lightEnv) {
}


bool ForwardPass::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    if (!loadSolidShaders()) return false;
    createSolidPSO(colorFmt, depthFmt, hasDepth);
    m_sceneUbo   = m_device.createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "FwdSceneUBO"));
    m_objectUbo  = m_device.createBuffer(BufferDesc::uniform(
        MeshDrawCommand::kObjectUboStride * 4096, "FwdObjUBO"));   // 4096 objects
    m_materialUbo = m_device.createBuffer(BufferDesc::uniform(256, "FwdMatUBO"));
    // 填充默认材质（白色、不透明），避免 descriptor 未更新错误
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

void ForwardPass::uploadSceneUBO(const PassContext& ctx) {
    // 应用 Vulkan clip space 修正（Z:[-1,1]→[0,1], Y 翻转）
    Mat4 clip = m_device.clipSpaceCorrectionMatrix();
    Mat4 view = m_camera.viewMatrix();
    Mat4 proj = m_camera.projectionMatrix();
    Mat4 vp   = clip * proj * view;
    Vec3 eye  = m_camera.eyePosition();
    auto* dl  = m_lightEnv.primaryDirectional();
    Vec3 ldir = dl ? glm::normalize(dl->direction) : Vec3(-0.3, -1.0, -0.4);

    // 列主序矩阵 → float[16]
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
    storeVec3(ubo.lightColor,  Vec3(1.0));
    storeVec3(ubo.ambientColor,Vec3(0.4));
    storeVec3(ubo.edgeColor,   Vec3(0.08, 0.08, 0.08));
    storeVec3(ubo.highlightColor, Vec3(1.0, 0.5, 0.0));

    ctx.cmd->updateBuffer(m_sceneUbo.get(), 0, sizeof(ubo), &ubo);
}

void ForwardPass::execute(const PassContext& ctx) {
    if (!m_initialized || !m_pso || !ctx.cmd) return;

    uploadSceneUBO(ctx);

    for (auto& cmd : m_commands) {
        if (!cmd.visible || cmd.instanceCount == 0) continue;
        cmd.execute(*ctx.cmd, m_sceneUbo.get(), m_objectUbo.get(), m_materialUbo.get());
    }
}

} // namespace mulan::engine
