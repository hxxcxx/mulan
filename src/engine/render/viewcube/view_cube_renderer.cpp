#include "view_cube_renderer.h"
#include "../../rhi/command_list.h"
#include "../../vertex/vertex_layout.h"
#include <mulan/math/math.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace mulan::engine {

// ============================================================
// 立方体几何常量
// ============================================================

static constexpr float kHalf = 0.5f;   // 半边长

/// 6个面定义：normal, up, 面颜色(线性), 边线颜色
static const struct {
    float normal[3];
    float up[3];
    float color[3];
} kFaces[6] = {
    // Front  (+Z)
    {{ 0, 0, 1},  { 0, 1, 0},  {0.35f, 0.65f, 0.35f}},
    // Back   (-Z)
    {{ 0, 0,-1},  { 0, 1, 0},  {0.65f, 0.35f, 0.35f}},
    // Left   (-X)
    {{-1, 0, 0},  { 0, 1, 0},  {0.35f, 0.35f, 0.65f}},
    // Right  (+X)
    {{ 1, 0, 0},  { 0, 1, 0},  {0.65f, 0.65f, 0.35f}},
    // Top    (+Y)
    {{ 0, 1, 0},  { 0, 0,-1},  {0.85f, 0.85f, 0.85f}},
    // Bottom (-Y)
    {{ 0,-1, 0},  { 0, 0, 1},  {0.45f, 0.45f, 0.45f}},
};

// ============================================================
// 构造 / 析构
// ============================================================

ViewCubeRenderer::ViewCubeRenderer(RHIDevice* device)
    : device_(device) {}

ViewCubeRenderer::~ViewCubeRenderer() {
    cleanup();
}

// ============================================================
// 初始化
// ============================================================

bool ViewCubeRenderer::init(TextureFormat colorFmt, TextureFormat depthFmt) {
    // 加载着色器（复用主场景 solid/edge 着色器）
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
    if (device_->backend() == GraphicsBackend::D3D12)
        ext = ".dxil";

    auto solidVsData = loadFile((shaderDir + "/solid.vert" + ext).c_str());
    auto solidFsData = loadFile((shaderDir + "/solid.frag" + ext).c_str());

    if (solidVsData.empty() || solidFsData.empty()) {
        std::fprintf(stderr, "[ViewCube] Failed to load solid shaders\n");
        return false;
    }

    {
        ShaderDesc desc;
        desc.type         = ShaderType::Vertex;
        desc.byteCode     = solidVsData.data();
        desc.byteCodeSize = static_cast<uint32_t>(solidVsData.size());
        auto r = device_->createShader(desc);
        if (!r) { std::fprintf(stderr, "[ViewCube] createShader solid.vert: %s\n", r.error().message.c_str()); return false; }
        solid_vs_ = std::move(*r);
    }
    {
        ShaderDesc desc;
        desc.type         = ShaderType::Pixel;
        desc.byteCode     = solidFsData.data();
        desc.byteCodeSize = static_cast<uint32_t>(solidFsData.size());
        auto r = device_->createShader(desc);
        if (!r) { std::fprintf(stderr, "[ViewCube] createShader solid.frag: %s\n", r.error().message.c_str()); return false; }
        solid_fs_ = std::move(*r);
    }

    if (!solid_vs_ || !solid_fs_) return false;

    // --- Solid PSO ---
    {
        VertexLayout layout;
        layout.begin()
            .add(VertexSemantic::Position,  VertexFormat::Float3)
            .add(VertexSemantic::Normal,    VertexFormat::Float3)
            .add(VertexSemantic::TexCoord0, VertexFormat::Float2);

        GraphicsPipelineDesc desc{};
        desc.name                  = "ViewCube_Solid";
        desc.vs                    = solid_vs_.get();
        desc.ps                    = solid_fs_.get();
        desc.vertexLayout          = layout;
        desc.topology              = PrimitiveTopology::TriangleList;
        desc.cullMode              = CullMode::None;       // 双面
        desc.frontFace             = FrontFace::CounterClockwise;
        desc.fillMode              = FillMode::Solid;
        desc.depthStencil.depthEnable = true;
        desc.depthStencil.depthWrite  = true;
        desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

        using PB = PipelineBinding;
        desc.descriptorBindings[0] = {.binding = 0, .count = 1,
                                      .type = DescriptorType::UniformBuffer,
                                      .stages = PB::kStageVertex | PB::kStageFragment};
        desc.descriptorBindings[1] = {.binding = 1, .count = 1,
                                      .type = DescriptorType::UniformBuffer,
                                      .stages = PB::kStageVertex | PB::kStageFragment};
        desc.descriptorBindings[2] = {.binding = 2, .count = 1,
                                      .type = DescriptorType::UniformBuffer,
                                      .stages = PB::kStageFragment};
        desc.descriptorBindingCount = 3;

        desc.colorFormats[0]      = colorFmt;
        desc.colorTargetCount     = 1;
        desc.depthStencilFormat   = depthFmt;
        desc.depthEnable          = true;

        auto psoR = device_->createPipelineState(desc);
        if (!psoR) { std::fprintf(stderr, "[ViewCube] createPSO solid: %s\n", psoR.error().message.c_str()); return false; }
        solid_pso_ = std::move(*psoR);
    }

    // --- Edge PSO ---
    auto edgeVsData = loadFile((shaderDir + "/edge.vert" + ext).c_str());
    auto edgeFsData = loadFile((shaderDir + "/edge.frag" + ext).c_str());

    if (!edgeVsData.empty() && !edgeFsData.empty()) {
        {
            ShaderDesc desc;
            desc.type         = ShaderType::Vertex;
            desc.byteCode     = edgeVsData.data();
            desc.byteCodeSize = static_cast<uint32_t>(edgeVsData.size());
            auto r = device_->createShader(desc);
            if (r) edge_vs_ = std::move(*r);
            else std::fprintf(stderr, "[ViewCube] createShader edge.vert: %s\n", r.error().message.c_str());
        }
        {
            ShaderDesc desc;
            desc.type         = ShaderType::Pixel;
            desc.byteCode     = edgeFsData.data();
            desc.byteCodeSize = static_cast<uint32_t>(edgeFsData.size());
            auto r = device_->createShader(desc);
            if (r) edge_fs_ = std::move(*r);
            else std::fprintf(stderr, "[ViewCube] createShader edge.frag: %s\n", r.error().message.c_str());
        }

        if (edge_vs_ && edge_fs_) {
            VertexLayout layout;
            layout.begin()
                .add(VertexSemantic::Position,  VertexFormat::Float3)
                .add(VertexSemantic::Normal,    VertexFormat::Float3)
                .add(VertexSemantic::TexCoord0, VertexFormat::Float2);

            GraphicsPipelineDesc desc{};
            desc.name                  = "ViewCube_Edge";
            desc.vs                    = edge_vs_.get();
            desc.ps                    = edge_fs_.get();
            desc.vertexLayout          = layout;
            desc.topology              = PrimitiveTopology::LineList;
            desc.cullMode              = CullMode::None;
            desc.frontFace             = FrontFace::CounterClockwise;
            desc.fillMode              = FillMode::Solid;
            desc.depthStencil.depthEnable = true;
            desc.depthStencil.depthWrite  = false;
            desc.depthStencil.depthFunc   = CompareFunc::LessEqual;

            using PB = PipelineBinding;
            desc.descriptorBindings[0] = {.binding = 0, .count = 1,
                                          .type = DescriptorType::UniformBuffer,
                                          .stages = PB::kStageVertex | PB::kStageFragment};
            desc.descriptorBindings[1] = {.binding = 1, .count = 1,
                                          .type = DescriptorType::UniformBuffer,
                                          .stages = PB::kStageVertex | PB::kStageFragment};
            desc.descriptorBindings[2] = {.binding = 2, .count = 1,
                                          .type = DescriptorType::UniformBuffer,
                                          .stages = PB::kStageFragment};
            desc.descriptorBindingCount = 3;

            desc.colorFormats[0]      = colorFmt;
            desc.colorTargetCount     = 1;
            desc.depthStencilFormat   = depthFmt;
            desc.depthEnable          = true;

            auto psoR = device_->createPipelineState(desc);
            if (psoR) edge_pso_ = std::move(*psoR);
            else std::fprintf(stderr, "[ViewCube] createPSO edge: %s\n", psoR.error().message.c_str());
        }
    }

    // --- UBO ---
    // 材质 UBO 需要按 minUniformBufferOffsetAlignment 对齐
    uint32_t uboAlign = device_->capabilities().minUniformBufferOffsetAlignment;
    if (uboAlign == 0) uboAlign = 256;
    auto alignUp32 = [](uint32_t value, uint32_t alignment) -> uint32_t {
        uint32_t mask = alignment - 1;
        return (value + mask) & ~mask;
    };
    material_stride_ = alignUp32(static_cast<uint32_t>(sizeof(MaterialGPU)), uboAlign);

    {
        auto r = device_->createBuffer(BufferDesc::uniform(sizeof(SceneUBO),   "ViewCube_SceneUBO"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer SceneUBO: %s\n", r.error().message.c_str()); return false; }
        scene_ubo_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(BufferDesc::uniform(sizeof(ObjectUBO),  "ViewCube_ObjectUBO"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer ObjectUBO: %s\n", r.error().message.c_str()); return false; }
        object_ubo_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(BufferDesc::uniform(material_stride_ * kFaceCount, "ViewCube_MaterialUBO"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer MaterialUBO: %s\n", r.error().message.c_str()); return false; }
        material_ubo_= std::move(*r);
    }

    if (!scene_ubo_ || !object_ubo_ || !material_ubo_) return false;

    // --- 初始化面材质 ---
    for (int i = 0; i < kFaceCount; ++i) {
        auto& m = face_materials_[i];
        std::memset(&m, 0, sizeof(m));
        m.baseColor[0] = kFaces[i].color[0];
        m.baseColor[1] = kFaces[i].color[1];
        m.baseColor[2] = kFaces[i].color[2];
        m.metallic     = 0.0f;
        m.roughness    = 0.8f;
        m.alpha        = 1.0f;
        m.materialType = 0;  // Unlit-like
    }

    // 逐面上传材质 UBO（使用对齐偏移）
    for (int i = 0; i < kFaceCount; ++i) {
        material_ubo_->update(i * material_stride_, sizeof(MaterialGPU), &face_materials_[i]);
    }

    // --- 几何体 ---
    if (!createGeometry()) return false;

    initialized_ = true;
    return true;
}

void ViewCubeRenderer::cleanup() {
    edge_ib_.reset();
    edge_vb_.reset();
    face_ib_.reset();
    face_vb_.reset();
    material_ubo_.reset();
    object_ubo_.reset();
    scene_ubo_.reset();
    edge_pso_.reset();
    edge_fs_.reset();
    edge_vs_.reset();
    solid_pso_.reset();
    solid_fs_.reset();
    solid_vs_.reset();
    initialized_ = false;
}

// ============================================================
// 几何体生成
// ============================================================

bool ViewCubeRenderer::createGeometry() {
    if (!createFaceGeometry()) return false;
    if (!createEdgeGeometry()) return false;
    return true;
}

bool ViewCubeRenderer::createFaceGeometry() {
    // 每个面 4 个顶点，6 个面 = 24 个顶点
    // 每个面 6 个索引（2个三角形），6 个面 = 36 个索引
    std::array<CubeVertex, 24> verts;
    std::array<uint32_t, 36>  indices;

    float h = kHalf;

    for (int f = 0; f < 6; ++f) {
        float nx = kFaces[f].normal[0];
        float ny = kFaces[f].normal[1];
        float nz = kFaces[f].normal[2];
        float ux = kFaces[f].up[0];
        float uy = kFaces[f].up[1];
        float uz = kFaces[f].up[2];

        // 计算 right = cross(normal, up)
        float rx = ny * uz - nz * uy;
        float ry = nz * ux - nx * uz;
        float rz = nx * uy - ny * ux;

        // 4 个角的中心偏移
        // 中心 = normal * h
        float cx = nx * h, cy = ny * h, cz = nz * h;

        // v0 = center - right*h - up*h
        // v1 = center + right*h - up*h
        // v2 = center + right*h + up*h
        // v3 = center - right*h + up*h
        auto makeVert = [&](float sr, float su) -> CubeVertex {
            return {
                cx + rx*sr*h + ux*su*h,
                cy + ry*sr*h + uy*su*h,
                cz + rz*sr*h + uz*su*h,
                nx, ny, nz,
                sr * 0.5f + 0.5f, su * 0.5f + 0.5f
            };
        };

        int base = f * 4;
        verts[base + 0] = makeVert(-1, -1);
        verts[base + 1] = makeVert( 1, -1);
        verts[base + 2] = makeVert( 1,  1);
        verts[base + 3] = makeVert(-1,  1);

        int ib = f * 6;
        indices[ib + 0] = base + 0;
        indices[ib + 1] = base + 1;
        indices[ib + 2] = base + 2;
        indices[ib + 3] = base + 0;
        indices[ib + 4] = base + 2;
        indices[ib + 5] = base + 3;
    }

    face_index_count_ = 36;

    {
        auto r = device_->createBuffer(
            BufferDesc::vertex(sizeof(CubeVertex) * verts.size(), verts.data(), "ViewCube_FaceVB"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer FaceVB: %s\n", r.error().message.c_str()); return false; }
        face_vb_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(
            BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_FaceIB"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer FaceIB: %s\n", r.error().message.c_str()); return false; }
        face_ib_ = std::move(*r);
    }
    return true;
}

bool ViewCubeRenderer::createEdgeGeometry() {
    // 12 条边，每条边 2 个顶点 = 24 个顶点
    // 12 条边，每条边 2 个索引 = 24 个索引
    float h = kHalf;

    // 8 个角点
    float corners[8][3] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},  // back face
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},  // front face
    };

    // 12 条边的索引对
    static constexpr int edgePairs[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},   // back
        {4,5}, {5,6}, {6,7}, {7,4},   // front
        {0,4}, {1,5}, {2,6}, {3,7},   // connecting
    };

    std::array<CubeVertex, 24> verts;
    std::array<uint32_t, 24>   indices;

    for (int i = 0; i < 12; ++i) {
        int a = edgePairs[i][0];
        int b = edgePairs[i][1];

        // 使用一个默认法线（边线渲染不依赖法线）
        verts[i * 2 + 0] = {corners[a][0], corners[a][1], corners[a][2], 0,1,0, 0,0};
        verts[i * 2 + 1] = {corners[b][0], corners[b][1], corners[b][2], 0,1,0, 1,0};

        indices[i * 2 + 0] = i * 2 + 0;
        indices[i * 2 + 1] = i * 2 + 1;
    }

    edge_index_count_ = 24;

    {
        auto r = device_->createBuffer(
            BufferDesc::vertex(sizeof(CubeVertex) * verts.size(), verts.data(), "ViewCube_EdgeVB"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer EdgeVB: %s\n", r.error().message.c_str()); return false; }
        edge_vb_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(
            BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_EdgeIB"));
        if (!r) { std::fprintf(stderr, "[ViewCube] createBuffer EdgeIB: %s\n", r.error().message.c_str()); return false; }
        edge_ib_ = std::move(*r);
    }
    return true;
}

// ============================================================
// 渲染
// ============================================================

void ViewCubeRenderer::render(CommandList* cmd, const math::Mat4& mainViewMatrix,
                               uint32_t vpWidth, uint32_t vpHeight) {
    if (!initialized_ || !cmd || !solid_pso_) return;

    // --- 1. 计算 ViewCube 视口（右下角）---
    uint32_t size   = cube_size_;
    uint32_t margin = margin_;
    int32_t vx = static_cast<int32_t>(vpWidth)  - static_cast<int32_t>(size + margin);
    int32_t vy = static_cast<int32_t>(vpHeight) - static_cast<int32_t>(size + margin);

    // --- 2. 设置 ViewCube 专用视口和裁剪 ---
    Viewport cubeVP{
        static_cast<float>(vx), static_cast<float>(vy),
        static_cast<float>(size), static_cast<float>(size),
        0.0f, 1.0f
    };
    ScissorRect cubeScissor{vx, vy, static_cast<int32_t>(size), static_cast<int32_t>(size)};

    cmd->setViewport(cubeVP);
    cmd->setScissorRect(cubeScissor);

    // --- 3. 构造 ViewCube 的 view/proj 矩阵 ---
    // 从主相机提取纯旋转（去除平移）
    const math::Mat4& mainView = mainViewMatrix;
    math::Mat3 rotOnly = math::Mat3(mainView);  // 提取左上 3x3 旋转

    // 构造 ViewCube view：纯旋转 + 固定距离后退
    math::Mat4 cubeView = math::Mat4(rotOnly);
    // 设置平移：沿 Z 后退固定距离
    cubeView[3] = math::Vec4(0, 0, -3.5, 1);

    // 正交投影：不受距离影响
    double orthoSize = 1.2;
    math::Mat4 cubeProj = math::Mat4::ortho(-orthoSize, orthoSize,
                          -orthoSize, orthoSize,
                          0.1, 10.0);

    // 应用 clip space 修正
    auto clip = device_->clipSpaceCorrectionMatrix();
    math::Mat4 corrProj = clip * cubeProj;
    math::Mat4 cubeVP_mat = corrProj * cubeView;

    // --- 4. 上传 SceneUBO ---
    SceneUBO sceneUbo{};
    std::memset(&sceneUbo, 0, sizeof(sceneUbo));

    auto storeMat4 = [](float* dst, const math::Mat4& src) {
        const double* p = src.data();
        for (int i = 0; i < 16; ++i)
            dst[i] = static_cast<float>(p[i]);
    };

    storeMat4(sceneUbo.view,           cubeView);
    storeMat4(sceneUbo.projection,     corrProj);
    storeMat4(sceneUbo.viewProjection, cubeVP_mat);

    // 光照：正面方向光 + 强环境光（ViewCube 是 UI 元素，所有面都需要可见）
    sceneUbo.lightDir[0]  = -0.3f;
    sceneUbo.lightDir[1]  = -1.0f;
    sceneUbo.lightDir[2]  = -0.4f;
    sceneUbo.lightColor[0] = 1.0f;
    sceneUbo.lightColor[1] = 1.0f;
    sceneUbo.lightColor[2] = 1.0f;
    sceneUbo.ambientColor[0] = 0.85f;
    sceneUbo.ambientColor[1] = 0.85f;
    sceneUbo.ambientColor[2] = 0.85f;
    sceneUbo.edgeColor[0] = 0.08f;
    sceneUbo.edgeColor[1] = 0.08f;
    sceneUbo.edgeColor[2] = 0.08f;

    scene_ubo_->update(0, sizeof(SceneUBO), &sceneUbo);

    // --- 5. 上传 ObjectUBO（单位矩阵）---
    ObjectUBO objUbo{};
    std::memset(&objUbo, 0, sizeof(objUbo));
    // 单位矩阵
    objUbo.world[0]  = 1.0f;
    objUbo.world[5]  = 1.0f;
    objUbo.world[10] = 1.0f;
    objUbo.world[15] = 1.0f;
    // 单位法线矩阵
    objUbo.normalMat[0] = 1.0f;
    objUbo.normalMat[5] = 1.0f;
    objUbo.normalMat[10]= 1.0f;
    object_ubo_->update(0, sizeof(ObjectUBO), &objUbo);

    // --- 6. 渲染立方体面（每面独立材质）---
    cmd->setPipelineState(solid_pso_.get());

    for (int f = 0; f < kFaceCount; ++f) {
        uint32_t matOffset = f * material_stride_;

        BindGroupDesc bg;
        bg.addUBO(0, scene_ubo_.get(),   0, sizeof(SceneUBO))
          .addUBO(1, object_ubo_.get(),   0, sizeof(ObjectUBO))
          .addUBO(2, material_ubo_.get(), matOffset, sizeof(MaterialGPU));
        cmd->bindResources(bg);

        cmd->setVertexBuffer(0, face_vb_.get());
        cmd->setIndexBuffer(face_ib_.get());

        // 每面 6 个索引
        cmd->drawIndexed(DrawIndexedAttribs{
            .indexCount  = 6,
            .startIndex  = static_cast<uint32_t>(f * 6),
            .baseVertex  = 0
        });
    }

    // --- 7. 渲染边线 ---
    if (edge_pso_ && edge_vb_ && edge_ib_) {
        cmd->setPipelineState(edge_pso_.get());

        BindGroupDesc bg;
        bg.addUBO(0, scene_ubo_.get(),   0, sizeof(SceneUBO))
          .addUBO(1, object_ubo_.get(),   0, sizeof(ObjectUBO))
          .addUBO(2, material_ubo_.get(), 0, sizeof(MaterialGPU));  // 材质不影响边线
        cmd->bindResources(bg);

        cmd->setVertexBuffer(0, edge_vb_.get());
        cmd->setIndexBuffer(edge_ib_.get());

        cmd->drawIndexed(DrawIndexedAttribs{
            .indexCount = edge_index_count_
        });
    }

    // --- 8. 恢复全屏视口和裁剪 ---
    cmd->setViewport({0.0f, 0.0f,
                      static_cast<float>(vpWidth),
                      static_cast<float>(vpHeight),
                      0.0f, 1.0f});
    cmd->setScissorRect({0, 0,
                         static_cast<int32_t>(vpWidth),
                         static_cast<int32_t>(vpHeight)});
}

// ============================================================
// 交互预留（空实现）
// ============================================================

bool ViewCubeRenderer::hitTest(int screenX, int screenY,
                                uint32_t vpWidth, uint32_t vpHeight) const {
    // TODO: 检测屏幕坐标是否在 ViewCube 区域内
    (void)screenX; (void)screenY; (void)vpWidth; (void)vpHeight;
    return false;
}

std::optional<ViewCubeFace> ViewCubeRenderer::hitTestFace(int screenX, int screenY,
                                                           uint32_t vpWidth, uint32_t vpHeight) const {
    // TODO: 检测点击了哪个面
    (void)screenX; (void)screenY; (void)vpWidth; (void)vpHeight;
    return std::nullopt;
}

void ViewCubeRenderer::snapToFace(ViewCubeFace face, Camera& camera) {
    // TODO: 动画过渡到指定面的标准视图
    (void)face; (void)camera;
}

} // namespace mulan::engine
