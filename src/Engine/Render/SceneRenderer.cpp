/**
 * @file SceneRenderer.cpp
 * @brief SceneRenderer 实现 — 管理管线资源，录制 RHI 绘制命令
 * @author hxxcxx
 * @date 2026-04-15
 */
#include "SceneRenderer.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// 常量
// ============================================================

static constexpr float kHighlightColor[] = {0.3f, 0.6f, 1.0f};
static constexpr float kEdgeColor[]     = {0.10f, 0.10f, 0.10f};

// ============================================================
// 构造
// ============================================================

SceneRenderer::SceneRenderer(RHIDevice* device)
    : m_device(device) {}

// ============================================================
// 初始化
// ============================================================

bool SceneRenderer::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    loadShaders();
    if (!m_solidVs || !m_solidFs) return false;

    createPSOs(colorFmt, depthFmt, hasDepth);
    createUBOs();

    // 创建 Pass 管线
    m_forwardPass = std::make_unique<ForwardPass>(*this);
    m_passes.push_back(m_forwardPass.get());

    // 文字渲染器
    m_textRenderer = std::make_unique<TextRenderer>(m_device);
    m_textRenderer->init(colorFmt, depthFmt);

    return true;
}

void SceneRenderer::cleanup() {
    m_passes.clear();
    m_forwardPass.reset();
    m_textRenderer.reset();
    m_instances.clear();
    m_defaultMaterial = nullptr;
    m_materialBuffer.reset();
    m_objectBuffer.reset();
    m_sceneBuffer.reset();
    m_edgePso.reset();
    m_edgeFs.reset();
    m_edgeVs.reset();
    m_solidPso.reset();
    m_solidFs.reset();
    m_solidVs.reset();
}

// ============================================================
// Shader
// ============================================================

void SceneRenderer::loadShaders() {
    auto loadFile = [](const char* path) -> std::vector<uint8_t> {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, path, "rb");
#else
        f = fopen(path, "rb");
#endif
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> data(size);
        fread(data.data(), 1, size, f);
        fclose(f);
        return data;
    };

#ifdef SHADER_DIR
    std::string shaderDir = SHADER_DIR;
#else
    std::string shaderDir = "shaders";
#endif

    const char* ext = ".spv";
    if (m_device->backend() == GraphicsBackend::D3D12)
        ext = ".dxil";
    else if (m_device->backend() == GraphicsBackend::D3D11)
        ext = ".dxbc";

    char pathBuf[512];
    auto checkLoad = [&](const char* name, const std::vector<uint8_t>& data) {
        if (data.empty()) {
            snprintf(pathBuf, sizeof(pathBuf), "%s/%s%s", shaderDir.c_str(), name, ext);
            std::fprintf(stderr, "[SceneRenderer] Failed to load shader: %s\n", pathBuf);
        }
    };

    auto solidVsData = loadFile((shaderDir + "/solid.vert" + ext).c_str());
    auto solidFsData = loadFile((shaderDir + "/solid.frag" + ext).c_str());
    auto edgeVsData  = loadFile((shaderDir + "/edge.vert" + ext).c_str());
    auto edgeFsData  = loadFile((shaderDir + "/edge.frag" + ext).c_str());

    checkLoad("solid.vert", solidVsData);
    checkLoad("solid.frag", solidFsData);
    checkLoad("edge.vert", edgeVsData);
    checkLoad("edge.frag", edgeFsData);

    ShaderDesc vsDesc;
    vsDesc.type         = ShaderType::Vertex;
    vsDesc.byteCode     = solidVsData.data();
    vsDesc.byteCodeSize = static_cast<uint32_t>(solidVsData.size());
    m_solidVs = m_device->createShader(vsDesc);

    ShaderDesc fsDesc;
    fsDesc.type         = ShaderType::Pixel;
    fsDesc.byteCode     = solidFsData.data();
    fsDesc.byteCodeSize = static_cast<uint32_t>(solidFsData.size());
    m_solidFs = m_device->createShader(fsDesc);

    if (!edgeVsData.empty() && !edgeFsData.empty()) {
        ShaderDesc evsDesc;
        evsDesc.type         = ShaderType::Vertex;
        evsDesc.byteCode     = edgeVsData.data();
        evsDesc.byteCodeSize = static_cast<uint32_t>(edgeVsData.size());
        m_edgeVs = m_device->createShader(evsDesc);

        ShaderDesc efsDesc;
        efsDesc.type         = ShaderType::Pixel;
        efsDesc.byteCode     = edgeFsData.data();
        efsDesc.byteCodeSize = static_cast<uint32_t>(edgeFsData.size());
        m_edgeFs = m_device->createShader(efsDesc);
    }
}

// ============================================================
// PSO
// ============================================================

void SceneRenderer::createPSOs(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth) {
    m_vertexLayout.begin()
        .add(VertexSemantic::Position,  VertexFormat::Float3)
        .add(VertexSemantic::Normal,    VertexFormat::Float3)
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2);

    auto makeDesc = [&](Shader* vs, Shader* ps, PrimitiveTopology topo) {
        GraphicsPipelineDesc desc{};
        desc.name                  = (topo == PrimitiveTopology::LineList) ? "Edge" : "Solid";
        desc.vs                    = vs;
        desc.ps                    = ps;
        desc.vertexLayout          = m_vertexLayout;
        desc.topology              = topo;
        desc.cullMode              = CullMode::None;
        desc.frontFace             = FrontFace::CounterClockwise;
        desc.fillMode              = FillMode::Solid;
        desc.depthStencil.depthEnable = true;
        desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

        using PB = PipelineBinding;
        desc.descriptorBindings[0] = {.binding = 0, .count = 1, .stages = PB::kStageVertex | PB::kStageFragment};
        desc.descriptorBindings[1] = {.binding = 1, .count = 1, .stages = PB::kStageVertex | PB::kStageFragment};
        desc.descriptorBindings[2] = {.binding = 2, .count = 1, .type = DescriptorType::UniformBuffer, .stages = PB::kStageFragment};
        desc.descriptorBindingCount = 3;

        // 渲染目标格式
        desc.colorFormats[0]  = colorFmt;
        desc.colorTargetCount = 1;
        desc.depthStencilFormat = depthFmt;
        desc.depthEnable        = hasDepth;

        return desc;
    };

    auto solidDesc = makeDesc(m_solidVs.get(), m_solidFs.get(),
                              PrimitiveTopology::TriangleList);
    solidDesc.depthStencil.depthWrite = true;
    m_solidPso = m_device->createPipelineState(solidDesc);

    if (m_edgeVs && m_edgeFs) {
        auto edgeDesc = makeDesc(m_edgeVs.get(), m_edgeFs.get(),
                                 PrimitiveTopology::LineList);
        edgeDesc.depthStencil.depthWrite              = false;
        edgeDesc.depthStencil.depthBias               = 1.0f;
        edgeDesc.depthStencil.depthBiasClamp          = 0.0f;
        edgeDesc.depthStencil.slopeScaledDepthBias    = 1.5f;
        m_edgePso = m_device->createPipelineState(edgeDesc);
    }
}

// ============================================================
// UBO 创建 (per-frame ring buffer)
// ============================================================

void SceneRenderer::createUBOs() {
    uint32_t align = m_device->capabilities().minUniformBufferOffsetAlignment;
    if (align == 0) align = 256;

    auto alignUp32 = [](uint32_t value, uint32_t alignment) -> uint32_t {
        uint32_t mask = alignment - 1;
        return (value + mask) & ~mask;
    };
    m_objectStride   = alignUp32(static_cast<uint32_t>(sizeof(ObjectUBO)),   align);
    m_materialStride = alignUp32(static_cast<uint32_t>(sizeof(MaterialUBO)), align);

    m_sceneBuffer = m_device->createBuffer(
        BufferDesc::uniform(sizeof(SceneUBO), "SceneUBO"));

    m_objectBuffer = m_device->createBuffer(
        BufferDesc::uniform(kMaxDrawCalls * m_objectStride, "ObjectUBO_Ring"));

    m_materialBuffer = m_device->createBuffer(
        BufferDesc::uniform(kMaxDrawCalls * m_materialStride, "MaterialUBO_Ring"));

    // 默认光照
    if (m_lightEnv.lightCount == 0) {
        m_lightEnv.addLight(Light::directional({-0.3, -1.0, -0.4}, {1, 1, 1}, 1.0));
    }

    // 初始化 slot[0] 为默认材质，避免首帧读到零值
    {
        auto defaultGpu = MaterialGPU::fromMaterial(Material::defaultPBR());
        m_materialBuffer->update(0, sizeof(MaterialUBO), &defaultGpu);
    }

    // 默认材质实例
    auto* defaultAsset = MaterialCache::instance().defaultPBR();
    if (defaultAsset) {
        m_defaultMaterial = getOrCreateInstance(defaultAsset->id());
    } else {
        auto inst = std::make_unique<MaterialInstance>(Material::defaultPBR());
        m_defaultMaterial = inst.get();
        m_instances[0] = std::move(inst);
    }
}

// ============================================================
// SceneUBO 更新 (每帧一次)
// ============================================================

void SceneRenderer::updateSceneUBO(const Camera& camera) {
    if (!m_sceneBuffer) return;

    SceneUBO ubo{};

    auto view     = camera.viewMatrix();
    auto proj     = camera.projectionMatrix();
    auto clip     = m_device->clipSpaceCorrectionMatrix();
    auto corrProj = clip * proj;
    auto viewProj = corrProj * view;

    auto storeMat4 = [](float* dst, const Mat4& src) {
        const double* p = glm::value_ptr(src);
        for (int i = 0; i < 16; ++i)
            dst[i] = static_cast<float>(p[i]);
    };

    storeMat4(ubo.view,           view);
    storeMat4(ubo.projection,     corrProj);
    storeMat4(ubo.viewProjection, viewProj);

    auto pos = camera.eyePosition();
    ubo.cameraPos[0] = static_cast<float>(pos.x);
    ubo.cameraPos[1] = static_cast<float>(pos.y);
    ubo.cameraPos[2] = static_cast<float>(pos.z);

    // 光照
    if (auto* dl = m_lightEnv.primaryDirectional()) {
        ubo.lightDir[0] = static_cast<float>(dl->direction.x);
        ubo.lightDir[1] = static_cast<float>(dl->direction.y);
        ubo.lightDir[2] = static_cast<float>(dl->direction.z);
        float intensity = static_cast<float>(dl->intensity) * 3.5f;
        ubo.lightColor[0] = static_cast<float>(dl->color.x) * intensity;
        ubo.lightColor[1] = static_cast<float>(dl->color.y) * intensity;
        ubo.lightColor[2] = static_cast<float>(dl->color.z) * intensity;
    }
    ubo.ambientColor[0] = static_cast<float>(m_lightEnv.ambientColor.x * m_lightEnv.ambientIntensity);
    ubo.ambientColor[1] = static_cast<float>(m_lightEnv.ambientColor.y * m_lightEnv.ambientIntensity);
    ubo.ambientColor[2] = static_cast<float>(m_lightEnv.ambientColor.z * m_lightEnv.ambientIntensity);

    // 显示设置
    ubo.edgeColor[0]      = kEdgeColor[0];
    ubo.edgeColor[1]      = kEdgeColor[1];
    ubo.edgeColor[2]      = kEdgeColor[2];
    ubo.highlightColor[0] = kHighlightColor[0];
    ubo.highlightColor[1] = kHighlightColor[1];
    ubo.highlightColor[2] = kHighlightColor[2];

    m_sceneBuffer->update(0, sizeof(SceneUBO), &ubo);
}

// ============================================================
// 材质实例缓存
// ============================================================

MaterialInstance* SceneRenderer::getOrCreateInstance(uint32_t materialId) {
    auto it = m_instances.find(materialId);
    if (it != m_instances.end())
        return it->second.get();

    auto* asset = MaterialCache::instance().findById(materialId);
    if (!asset) return nullptr;

    auto inst = std::make_unique<MaterialInstance>(asset);
    auto* ptr = inst.get();
    m_instances[materialId] = std::move(inst);
    return ptr;
}

// ============================================================
// 渲染
// ============================================================

void SceneRenderer::render(const RenderQueue& queue, const Camera& camera, CommandList* cmdList,
                           const LightEnvironment& lightEnv) {
    m_stats = {};
    m_lightEnv = lightEnv;
    m_drawCallIndex = 0;

    // 强制第一帧的首次 MaterialUBO 写入（与任何真实状态不同）
    m_currentMatState = {0, true};

    if (m_lightEnv.lightCount == 0) {
        m_lightEnv.addLight(Light::directional({-0.3, -1.0, -0.4}));
    }

    if (!m_solidPso) return;

    // 更新 SceneUBO (每帧一次)
    updateSceneUBO(camera);

    Viewport vp{0.0f, 0.0f,
                 static_cast<float>(camera.width()),
                 static_cast<float>(camera.height()),
                 0.0f, 1.0f};
    cmdList->setViewport(vp);

    // 构建 PassContext，委托给 Pass 管线
    PassContext ctx(m_drawCallIndex, m_stats);
    ctx.device          = m_device;
    ctx.cmd             = cmdList;
    ctx.queue           = &queue;
    ctx.sceneBuffer     = m_sceneBuffer.get();
    ctx.objectBuffer    = m_objectBuffer.get();
    ctx.materialBuffer  = m_materialBuffer.get();
    ctx.sceneOffset     = 0;
    ctx.frameBaseIndex  = 0;
    ctx.solidPso        = m_solidPso.get();
    ctx.edgePso         = m_edgePso.get();

    for (auto* pass : m_passes)
        pass->execute(ctx);

    // 文字绘制（在所有 pass 之后，叠加在场景上方）
    if (m_textRenderer && m_font) {
        m_textRenderer->render(cmdList,
                               static_cast<uint32_t>(camera.width()),
                               static_cast<uint32_t>(camera.height()));
    }
}

// ============================================================
// drawItem
// ============================================================

void SceneRenderer::drawItem(const RenderItem& item, CommandList* cmdList,
                             PipelineState* pso, bool isEdge) {
    if (!item.geometry) return;

    const auto* gpu = item.gpu;
    if (!gpu || !gpu->vertexBuffer) return;

    if (!m_objectBuffer || !pso) return;

    // 超过 ring buffer 容量时跳过（防止覆盖 in-flight 数据）
    if (m_drawCallIndex >= kMaxDrawCalls) {
        ++m_stats.items;
        return;
    }

    uint32_t objOffset = m_drawCallIndex * m_objectStride;
    uint32_t matOffset = m_drawCallIndex * m_materialStride;

    // --- 1. 写 ObjectUBO (每次必须) ---

    ObjectUBO obj{};
    for (int i = 0; i < 16; ++i)
        obj.world[i] = static_cast<float>(glm::value_ptr(item.worldTransform)[i]);

    Mat3 normalMat3 = glm::transpose(glm::inverse(Mat3(item.worldTransform)));
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            obj.normalMat[col * 4 + row] = static_cast<float>(normalMat3[col][row]);

    obj.pickId   = item.pickId;
    obj.selected = item.selected ? 1u : 0u;
    m_objectBuffer->update(objOffset, sizeof(ObjectUBO), &obj);

    // --- 2. 写 MaterialUBO (仅不透明/半透明，边线跳过) ---

    if (!isEdge && m_materialBuffer) {
        MaterialState newState{item.materialIndex, item.selected};

        if (newState != m_currentMatState) {
            // 查找材质实例
            MaterialInstance* instance = nullptr;
            if (item.materialIndex != 0xFFFF)
                instance = getOrCreateInstance(item.materialIndex);
            if (!instance)
                instance = m_defaultMaterial;

            // 选中高亮
            if (item.selected) {
                instance->setHighlight({kHighlightColor[0],
                                        kHighlightColor[1],
                                        kHighlightColor[2]});
            } else {
                instance->clearHighlight();
            }

            // MaterialGPU (80B) 直传
            auto gpuMat = instance->toGPU();
            m_materialBuffer->update(matOffset, sizeof(MaterialUBO), &gpuMat);
            m_currentMatState = newState;
            ++m_stats.materialWrites;
        }
    }

    // --- 3. 绑定 UBO ---

    BindGroup bindGroup;
    bindGroup.addUBO(0, m_sceneBuffer.get(),    0,          sizeof(SceneUBO))
             .addUBO(1, m_objectBuffer.get(),   objOffset,  sizeof(ObjectUBO))
             .addUBO(2, m_materialBuffer.get(), matOffset,  sizeof(MaterialUBO));
    cmdList->bindResources(bindGroup);

    // --- 4. 绘制 ---

    cmdList->setVertexBuffer(0, gpu->vertexBuffer.get());

    if (gpu->indexBuffer && gpu->indexCount > 0) {
        cmdList->setIndexBuffer(gpu->indexBuffer.get());
        cmdList->drawIndexed(DrawIndexedAttribs{ .indexCount = gpu->indexCount });
        if (isEdge) m_stats.lines += gpu->indexCount / 2;
        else        m_stats.triangles += gpu->indexCount / 3;
    } else {
        cmdList->draw(DrawAttribs{ .vertexCount = gpu->vertexCount });
        if (isEdge) m_stats.lines += gpu->vertexCount / 2;
        else        m_stats.triangles += gpu->vertexCount / 3;
    }

    ++m_stats.drawCalls;
    ++m_stats.items;
    ++m_drawCallIndex;
}

// ============================================================
// 文字
// ============================================================

void SceneRenderer::setFont(FontAtlas* font) {
    m_font = font;
    if (m_textRenderer && font)
        m_textRenderer->setFont(font);
}

void SceneRenderer::addText(std::string_view text,
                             float x, float y,
                             float fontSize,
                             const float color[4]) {
    if (m_textRenderer)
        m_textRenderer->addText(text, x, y, fontSize, color);
}

} // namespace mulan::Engine
