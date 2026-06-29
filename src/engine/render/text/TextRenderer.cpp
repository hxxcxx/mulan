/**
 * @file TextRenderer.cpp
 * @brief MSDF 文字渲染器实现 — shader 加载、PSO 创建、绘制
 * @author hxxcxx
 * @date 2026-04-27
 */

#include "TextRenderer.h"
#include "TextLayout.h"

#include <cstdio>
#include <cstring>

namespace mulan::engine {

// ============================================================
// 构造 / 析构
// ============================================================

TextRenderer::TextRenderer(RHIDevice* device)
    : m_device(device) {}

TextRenderer::~TextRenderer() {
    cleanup();
}

// ============================================================
// 初始化
// ============================================================

bool TextRenderer::init(TextureFormat colorFmt, TextureFormat depthFmt) {
    loadShaders();
    if (!m_textVs || !m_textFs) {
        std::fprintf(stderr, "[TextRenderer] Failed to load text shaders\n");
        return false;
    }

    createPSO(colorFmt, depthFmt);
    if (!m_textPso) {
        std::fprintf(stderr, "[TextRenderer] Failed to create text PSO\n");
        return false;
    }

    // 创建 UBO
    m_textUbo = m_device->createBuffer(
        BufferDesc::uniform(sizeof(TextUBO), "TextUBO"));

    return m_textUbo != nullptr;
}

void TextRenderer::cleanup() {
    m_drawItems.clear();
    m_vertices.clear();
    m_indices.clear();
    m_textUbo.reset();
    m_indexBuffer.reset();
    m_vertexBuffer.reset();
    m_textPso.reset();
    m_textFs.reset();
    m_textVs.reset();
}

// ============================================================
// Shader 加载
// ============================================================

void TextRenderer::loadShaders() {
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

    auto vsData = loadFile((shaderDir + "/text.vert" + ext).c_str());
    auto fsData = loadFile((shaderDir + "/text.frag" + ext).c_str());

    if (vsData.empty())
        std::fprintf(stderr, "[TextRenderer] Failed to load: text.vert%s\n", ext);
    if (fsData.empty())
        std::fprintf(stderr, "[TextRenderer] Failed to load: text.frag%s\n", ext);

    if (!vsData.empty()) {
        ShaderDesc vsDesc;
        vsDesc.type         = ShaderType::Vertex;
        vsDesc.byteCode     = vsData.data();
        vsDesc.byteCodeSize = static_cast<uint32_t>(vsData.size());
        m_textVs = m_device->createShader(vsDesc);
    }

    if (!fsData.empty()) {
        ShaderDesc fsDesc;
        fsDesc.type         = ShaderType::Pixel;
        fsDesc.byteCode     = fsData.data();
        fsDesc.byteCodeSize = static_cast<uint32_t>(fsData.size());
        m_textFs = m_device->createShader(fsDesc);
    }
}

// ============================================================
// PSO 创建（带 Alpha 混合）
// ============================================================

void TextRenderer::createPSO(TextureFormat colorFmt, TextureFormat depthFmt) {
    // 顶点布局：pos(2f) + uv(2f) + color(uint) = 20 bytes
    VertexLayout layout;
    layout.begin()
        .add(VertexSemantic::Position,  VertexFormat::Float2)   // offset 0, 8 bytes
        .add(VertexSemantic::TexCoord0, VertexFormat::Float2)   // offset 8, 8 bytes
        .add(VertexSemantic::Color0,    VertexFormat::UInt);    // offset 16, 4 bytes

    GraphicsPipelineDesc desc{};
    desc.name         = "Text_MSDF";
    desc.vs           = m_textVs.get();
    desc.ps           = m_textFs.get();
    desc.vertexLayout = layout;
    desc.topology     = PrimitiveTopology::TriangleList;

    // 双面渲染
    desc.cullMode     = CullMode::None;
    desc.frontFace    = FrontFace::CounterClockwise;
    desc.fillMode     = FillMode::Solid;

    // 深度测试只读（文字不遮挡其他物体）
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

    // Alpha 混合
    desc.blend.independentBlend = false;
    auto& rt = desc.blend.renderTargets[0];
    rt.blendEnable   = true;
    rt.srcBlend      = BlendFactor::SrcAlpha;
    rt.dstBlend      = BlendFactor::InvSrcAlpha;
    rt.blendOp       = BlendOp::Add;
    rt.srcBlendAlpha = BlendFactor::One;
    rt.dstBlendAlpha = BlendFactor::InvSrcAlpha;
    rt.blendOpAlpha  = BlendOp::Add;
    rt.writeMask     = 0x0F;  // RGBA

    // 资源绑定：b0=UBO, t0=Atlas纹理, s0=采样器
    using PB = PipelineBinding;
    desc.descriptorBindings[0] = {.binding = 0, .count = 1,
                                  .type = DescriptorType::UniformBuffer,
                                  .stages = PB::kStageVertex | PB::kStageFragment};
    desc.descriptorBindings[1] = {.binding = 0, .count = 1,
                                  .type = DescriptorType::TextureSRV,
                                  .stages = PB::kStageFragment};
    desc.descriptorBindings[2] = {.binding = 0, .count = 1,
                                  .type = DescriptorType::Sampler,
                                  .stages = PB::kStageFragment};
    desc.descriptorBindingCount = 3;

    desc.colorFormats[0]      = colorFmt;
    desc.colorTargetCount     = 1;
    desc.depthStencilFormat   = depthFmt;
    desc.depthEnable          = true;

    m_textPso = m_device->createPipelineState(desc);
}

// ============================================================
// 设置字体
// ============================================================

void TextRenderer::setFont(FontAtlas* font) {
    m_font = font;
}

// ============================================================
// 添加文字
// ============================================================

void TextRenderer::addText(std::string_view text,
                            float x, float y,
                            float fontSize,
                            const float color[4]) {
    TextDrawItem item;
    item.text     = std::string(text);
    item.x        = x;
    item.y        = y;
    item.fontSize = fontSize;
    if (color) {
        memcpy(item.color, color, sizeof(item.color));
    } else {
        item.color[0] = 1;
        item.color[1] = 1;
        item.color[2] = 1;
        item.color[3] = 1;
    }
    m_drawItems.push_back(std::move(item));
}

// ============================================================
// 渲染
// ============================================================

void TextRenderer::render(CommandList* cmd, uint32_t width, uint32_t height) {
    if (m_drawItems.empty() || !m_font || !m_textPso || !cmd) return;

    // --- 1. 排版所有文字 → 顶点/索引 ---
    m_vertices.clear();
    m_indices.clear();

    for (const auto& item : m_drawItems) {
        TextLayout::layout(*m_font, item.text,
                           item.x, item.y, item.fontSize, item.color,
                           m_vertices, m_indices);
    }

    if (m_vertices.empty()) {
        m_drawItems.clear();
        return;
    }

    // --- 2. 更新 TextUBO：正交投影矩阵 ---
    // 屏幕空间：左上(0,0)，右下(width, height)，Y 向下
    float l = 0, r = static_cast<float>(width);
    float t = 0, b = static_cast<float>(height);
    // 正交投影：clip(x) = 2*(x-l)/(r-l)-1, clip(y) = 2*(y-t)/(b-t)-1
    // 但 NDC Y 向上，屏幕 Y 向下，需要翻转 Y
    float ortho[16] = {
        2.0f/(r-l),  0,            0, 0,
        0,           2.0f/(t-b),   0, 0,   // Y 翻转：t-b < 0
        0,           0,           -1, 0,
       -(r+l)/(r-l),-(t+b)/(t-b),  0, 1
    };

    TextUBO ubo;
    memcpy(ubo.orthoProjection, ortho, sizeof(ortho));
    ubo.bgColor[0] = 0;
    ubo.bgColor[1] = 0;
    ubo.bgColor[2] = 0;
    ubo.bgColor[3] = 0;
    ubo.pxRange    = m_font->pxRange();
    memset(ubo._pad, 0, sizeof(ubo._pad));
    m_textUbo->update(0, sizeof(TextUBO), &ubo);

    // --- 3. 确保顶点/索引缓冲容量 ---
    uint32_t vbSize = static_cast<uint32_t>(m_vertices.size() * sizeof(TextVertex));
    uint32_t ibSize = static_cast<uint32_t>(m_indices.size() * sizeof(uint32_t));

    if (vbSize > m_vertexCapacity || !m_vertexBuffer) {
        m_vertexCapacity = vbSize * 2;
        m_vertexBuffer = m_device->createBuffer(
            BufferDesc::dynamicVertex(m_vertexCapacity, "TextVB"));
    }
    if (ibSize > m_indexCapacity || !m_indexBuffer) {
        m_indexCapacity = ibSize * 2;
        m_indexBuffer = m_device->createBuffer(
            BufferDesc{std::string_view("TextIB"), m_indexCapacity,
                       BufferUsage::Dynamic,
                       BufferBindFlags::IndexBuffer, nullptr});
    }

    // --- 4. 上传顶点/索引数据 ---
    if (m_vertexBuffer)
        m_vertexBuffer->update(0, vbSize, m_vertices.data());
    if (m_indexBuffer)
        m_indexBuffer->update(0, ibSize, m_indices.data());

    // --- 5. 绑定 PSO + 资源 ---
    cmd->setPipelineState(m_textPso.get());

    BindGroup bindGroup;
    bindGroup.addUBO(0, m_textUbo.get(), 0, sizeof(TextUBO));
    if (m_font->atlasTexture())
        bindGroup.addTexture(1, m_font->atlasTexture());
    cmd->bindResources(bindGroup);

    // --- 6. 绘制 ---
    if (m_vertexBuffer)
        cmd->setVertexBuffer(0, m_vertexBuffer.get());
    if (m_indexBuffer)
        cmd->setIndexBuffer(m_indexBuffer.get());

    cmd->drawIndexed(DrawIndexedAttribs{
        .indexCount = static_cast<uint32_t>(m_indices.size())
    });

    // --- 7. 清空队列 ---
    m_drawItems.clear();
    m_vertices.clear();
    m_indices.clear();
}

// ============================================================
// 清空
// ============================================================

void TextRenderer::clear() {
    m_drawItems.clear();
    m_vertices.clear();
    m_indices.clear();
}

} // namespace mulan::engine
