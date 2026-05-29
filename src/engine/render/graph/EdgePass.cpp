/**
 * @file EdgePass.cpp
 * @brief 边线渲染 Pass 实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "EdgePass.h"
#include "../../rhi/BindGroup.h"
#include "../../rhi/RenderTypes.h"

#include <cstdio>
#include <string>
#include <vector>

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
    m_objectUbo = m_device.createBuffer(BufferDesc::uniform(128, "EdgeObjUBO"));
    m_initialized = true;
    return true;
}

// ─── Shader ────────────────────────────────────────────────────

static std::vector<uint8_t> loadFile(const char* path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(sz > 0 ? sz : 0);
    if (sz > 0) fread(d.data(), 1, sz, f);
    fclose(f);
    return d;
}

bool EdgePass::loadEdgeShaders() {
#ifdef SHADER_DIR
    std::string dir = SHADER_DIR;
#else
    std::string dir = "shaders";
#endif
    const char* ext = ".spv";
    if      (m_device.backend() == GraphicsBackend::D3D12) ext = ".dxil";
    else if (m_device.backend() == GraphicsBackend::D3D11) ext = ".dxbc";

    auto vs = loadFile((dir + "/edge.vert" + ext).c_str());
    auto fs = loadFile((dir + "/edge.frag" + ext).c_str());
    if (vs.empty() || fs.empty()) return false;

    ShaderDesc d;
    d.type = ShaderType::Vertex;
    d.byteCode = vs.data();
    d.byteCodeSize = static_cast<uint32_t>(vs.size());
    m_vs = m_device.createShader(d);

    d.type = ShaderType::Pixel;
    d.byteCode = fs.data();
    d.byteCodeSize = static_cast<uint32_t>(fs.size());
    m_fs = m_device.createShader(d);

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
        .stages = PB::kStageVertex};
    desc.descriptorBindingCount = 2;

    desc.colorFormats[0]    = colorFmt;
    desc.colorTargetCount   = 1;
    desc.depthStencilFormat = depthFmt;
    desc.depthEnable        = hasDepth;

    m_pso = m_device.createPipelineState(desc);
}

// ─── Execute ───────────────────────────────────────────────────

void EdgePass::uploadSceneUBO(const PassContext& ctx) {
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

void EdgePass::uploadObjectUBO(const Mat4& world, const PassContext& ctx) {
    float d[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            d[c * 4 + r] = static_cast<float>(world[c][r]);
    ctx.cmd->updateBuffer(m_objectUbo.get(), 0, sizeof(d), d);
}

void EdgePass::execute(const PassContext& ctx) {
    if (!m_initialized || !m_pso || !ctx.cmd || !m_batches) return;

    uploadSceneUBO(ctx);
    ctx.cmd->setPipelineState(m_pso.get());

    for (auto& batch : *m_batches)
        drawBatch(batch, ctx);
}

void EdgePass::drawBatch(const DrawBatch& batch, const PassContext& ctx) {
    for (auto& dk : batch.keys) {
        const GpuGeometry* geo = m_gpu.edgeGeometry(dk.key);
        if (!geo || !geo->isValid()) continue;

        BindGroup bg;
        bg.addUBO(0, m_sceneUbo.get(),  0, 256);
        bg.addUBO(1, m_objectUbo.get(), 0, 128);
        ctx.cmd->bindResources(bg);

        uploadObjectUBO(dk.worldTransform, ctx);

        ctx.cmd->setVertexBuffer(0, geo->vertexBuffer.get(), 0);

        if (geo->indexCount > 0 && geo->indexBuffer) {
            ctx.cmd->setIndexBuffer(geo->indexBuffer.get(), 0, IndexType::UInt32);
            DrawIndexedAttribs da{};
            da.indexCount = geo->indexCount;
            ctx.cmd->drawIndexed(da);
        } else {
            DrawAttribs da{};
            da.vertexCount = geo->vertexCount;
            ctx.cmd->draw(da);
        }
    }
}

} // namespace mulan::engine
