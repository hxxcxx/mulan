#include "view_cube_stage.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../engine_error_code.h"
#include "../gpu_scene_contract.h"

#include <mulan/core/result/error.h>
#include <mulan/graphics/vertex/vertex_layout.h>
#include <mulan/math/math.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace mulan::engine {

namespace layouts = graphics::layouts;

namespace {

bool hasLayoutBinding(const BindGroupLayout& layout, uint32_t binding) {
    for (const auto& entry : layout.entries()) {
        if (entry.binding == binding) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ============================================================
// 立方体几何常量
// ============================================================

static constexpr float kHoverColor[3] = { 0.98f, 0.62f, 0.18f };
static constexpr float kPressedColor[3] = { 1.0f, 0.78f, 0.28f };
static constexpr float kAxisColors[3][3] = {
    { 0.92f, 0.16f, 0.14f },  // +X
    { 0.18f, 0.68f, 0.22f },  // +Y
    { 0.18f, 0.38f, 0.95f },  // +Z
};

void viewCubePartBaseColor(const ViewCubePart& part, float out[3]) {
    switch (part.type) {
    case ViewCubePartType::Face:
        out[0] = 0.58f;
        out[1] = 0.58f;
        out[2] = 0.58f;
        break;
    case ViewCubePartType::Edge:
        out[0] = 0.50f;
        out[1] = 0.50f;
        out[2] = 0.50f;
        break;
    case ViewCubePartType::Corner:
        out[0] = 0.44f;
        out[1] = 0.44f;
        out[2] = 0.44f;
        break;
    case ViewCubePartType::None:
        out[0] = 0.58f;
        out[1] = 0.58f;
        out[2] = 0.58f;
        break;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

ViewCubeStage::ViewCubeStage(RHIDevice& device) : device_(&device) {
}

ViewCubeStage::~ViewCubeStage() {
    axis_ib_.reset();
    axis_vb_.reset();
    face_ib_.reset();
    face_vb_.reset();
    material_ubo_.reset();
    object_ubo_.reset();
    scene_ubo_.reset();
    initialized_ = false;
}

// ============================================================
// 初始化
// ============================================================

core::Result<void> ViewCubeStage::init(RHIDevice& device, const RenderTargetInfo&) {
    device_ = &device;

    // --- UBO ---
    uint32_t uboAlign = device_->capabilities().minUniformBufferOffsetAlignment;
    if (uboAlign == 0)
        uboAlign = 256;
    auto alignUp32 = [](uint32_t value, uint32_t alignment) -> uint32_t {
        return (value + alignment - 1) & ~(alignment - 1);
    };
    material_stride_ = alignUp32(static_cast<uint32_t>(sizeof(MaterialGPU)), uboAlign);

    {
        auto r = device_->createBuffer(BufferDesc::uniform(sizeof(SceneUniforms), "ViewCube_SceneUBO"));
        if (!r)
            return std::unexpected(r.error());
        scene_ubo_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(BufferDesc::uniform(sizeof(ObjectUniforms), "ViewCube_ObjectUBO"));
        if (!r)
            return std::unexpected(r.error());
        object_ubo_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(BufferDesc::uniform(material_stride_ * kMaterialCount, "ViewCube_MaterialUBO"));
        if (!r)
            return std::unexpected(r.error());
        material_ubo_ = std::move(*r);
    }

    // --- 初始化材质 ---
    for (uint32_t i = 0; i < kMaterialCount; ++i) {
        auto& m = materials_[i];
        std::memset(&m, 0, sizeof(m));
        m.metallic = 0.0f;
        m.roughness = 0.8f;
        m.alpha = 1.0f;
        m.ao = 1.0f;  // 无 AO 纹理时必须为 1，否则 PBR 的 IBL 环境光被乘 0 → 背光面纯黑
        m.materialType = 0;
    }

    for (uint32_t i = 0; i < kPartCount; ++i) {
        viewCubePartBaseColor(ViewCubeModel::parts()[i], materials_[i].baseColor);
    }
    for (uint32_t axis = 0; axis < kAxisCount; ++axis) {
        materials_[kAxisMaterialOffset + axis].baseColor[0] = kAxisColors[axis][0];
        materials_[kAxisMaterialOffset + axis].baseColor[1] = kAxisColors[axis][1];
        materials_[kAxisMaterialOffset + axis].baseColor[2] = kAxisColors[axis][2];
    }

    for (uint32_t i = 0; i < kMaterialCount; ++i)
        material_ubo_->update(i * material_stride_, sizeof(MaterialGPU), &materials_[i]);

    // --- 几何体 ---
    if (!createGeometry()) {
        return std::unexpected(
                makeError(EngineErrorCode::PipelineCreateFailed, "ViewCubeStage geometry creation failed"));
    }

    initialized_ = true;
    return {};
}

void ViewCubeStage::shutdown(RHIDevice&) {
    face_bg_.reset();
    axis_ib_.reset();
    axis_vb_.reset();
    face_ib_.reset();
    face_vb_.reset();
    material_ubo_.reset();
    object_ubo_.reset();
    scene_ubo_.reset();
    initialized_ = false;
}

void ViewCubeStage::setPipelines(PipelineState* solidPso, PipelineState* edgePso) {
    solid_pso_ = solidPso;
    (void) edgePso;
}

void ViewCubeStage::setFallbackResources(Texture* defaultWhite, Sampler* defaultSampler) {
    default_white_ = defaultWhite;
    default_sampler_ = defaultSampler;
}

void ViewCubeStage::setSize(uint32_t size) {
    ViewCubeLayout layout = model_.layout();
    layout.size = size;
    model_.setLayout(layout);
}

void ViewCubeStage::setMargin(uint32_t margin) {
    ViewCubeLayout layout = model_.layout();
    layout.margin = margin;
    model_.setLayout(layout);
}

void ViewCubeStage::setLayout(const ViewCubeLayout& layout) {
    model_.setLayout(layout);
}

void ViewCubeStage::setInteraction(const ViewCubeInteractionState& interaction) {
    interaction_ = interaction;
}

void ViewCubeStage::setCorner(ViewCubeCorner corner) {
    ViewCubeLayout layout = model_.layout();
    layout.corner = corner;
    model_.setLayout(layout);
}

ViewCubeRect ViewCubeStage::viewportRect(uint32_t vpWidth, uint32_t vpHeight) const {
    return model_.viewportRect(vpWidth, vpHeight);
}

ViewCubeHit ViewCubeStage::pick(int screenX, int screenY, uint32_t vpWidth, uint32_t vpHeight) const {
    return model_.hitTest(screenX, screenY, vpWidth, vpHeight);
}

void ViewCubeStage::execute(RenderFrame& frame) {
    if (!frame.view.showViewCube)
        return;
    render(&frame.cmd, frame.view.viewMatrix, frame.view.width, frame.view.height);
}

// ============================================================
// 几何体生成
// ============================================================

bool ViewCubeStage::createGeometry() {
    if (!createFaceGeometry())
        return false;
    if (!createAxisGeometry())
        return false;
    return true;
}

bool ViewCubeStage::createFaceGeometry() {
    // 6个面中心 + 12个共享边区 + 8个角区均来自 ViewCubeModel::partShape()。
    // 绘制几何和拾取几何共用同一份拓扑定义，避免屏幕命中区域与实际显示区域不一致。
    std::vector<CubeVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(ViewCubeModel::kPartCount * 4);
    indices.reserve(ViewCubeModel::kPartCount * 6);
    part_index_offsets_.fill(0);
    part_index_counts_.fill(0);

    auto makeVert = [](const math::Vec3& p, const math::Vec3& n) -> CubeVertex {
        return { static_cast<float>(p.x),
                 static_cast<float>(p.y),
                 static_cast<float>(p.z),
                 static_cast<float>(n.x),
                 static_cast<float>(n.y),
                 static_cast<float>(n.z),
                 0.0f,
                 0.0f };
    };

    for (const auto& part : ViewCubeModel::parts()) {
        const ViewCubePartShape shape = ViewCubeModel::partShape(part);
        if (shape.vertexCount < 3 || shape.vertexCount > 4)
            continue;

        const math::Vec3 normal = ViewCubeModel::partNormal(part);
        const uint32_t base = static_cast<uint32_t>(verts.size());
        const uint32_t start = static_cast<uint32_t>(indices.size());

        for (uint32_t i = 0; i < shape.vertexCount; ++i) {
            verts.push_back(makeVert(shape.vertices[i], normal));
        }

        if (shape.vertexCount == 3) {
            indices.insert(indices.end(), { base, base + 1, base + 2 });
        } else {
            indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }

        part_index_offsets_[part.index] = start;
        part_index_counts_[part.index] = static_cast<uint32_t>(indices.size()) - start;
    }

    face_index_count_ = static_cast<uint32_t>(indices.size());

    {
        auto r = device_->createBuffer(
                BufferDesc::vertex(sizeof(CubeVertex) * verts.size(), verts.data(), "ViewCube_FaceVB"));
        if (!r) {
            std::fprintf(stderr, "[ViewCube] createBuffer FaceVB: %s\n", r.error().message.c_str());
            return false;
        }
        face_vb_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(
                BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_FaceIB"));
        if (!r) {
            std::fprintf(stderr, "[ViewCube] createBuffer FaceIB: %s\n", r.error().message.c_str());
            return false;
        }
        face_ib_ = std::move(*r);
    }
    return true;
}

bool ViewCubeStage::createAxisGeometry() {
    constexpr float frameMin = -0.78f;
    constexpr float frameMax = 1.06f;
    constexpr float coneLength = 0.18f;
    constexpr float shaftRadius = 0.018f;
    constexpr float coneRadius = 0.065f;

    const math::FVec3 starts[kAxisCount] = {
        math::FVec3(frameMin, frameMin, frameMin),
        math::FVec3(frameMin, frameMin, frameMin),
        math::FVec3(frameMin, frameMin, frameMin),
    };
    const math::FVec3 ends[kAxisCount] = {
        math::FVec3(frameMax, frameMin, frameMin),
        math::FVec3(frameMin, frameMax, frameMin),
        math::FVec3(frameMin, frameMin, frameMax),
    };
    const math::FVec3 dirs[kAxisCount] = {
        math::FVec3(1.0f, 0.0f, 0.0f),
        math::FVec3(0.0f, 1.0f, 0.0f),
        math::FVec3(0.0f, 0.0f, 1.0f),
    };
    const math::FVec3 sideA[kAxisCount] = {
        math::FVec3(0.0f, 1.0f, 0.0f),
        math::FVec3(1.0f, 0.0f, 0.0f),
        math::FVec3(1.0f, 0.0f, 0.0f),
    };
    const math::FVec3 sideB[kAxisCount] = {
        math::FVec3(0.0f, 0.0f, 1.0f),
        math::FVec3(0.0f, 0.0f, 1.0f),
        math::FVec3(0.0f, 1.0f, 0.0f),
    };

    std::vector<CubeVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(kAxisCount * kAxisSegments * 5);
    indices.reserve(kAxisCount * kAxisSegments * 9);

    auto makeVert = [](const math::FVec3& p, const math::FVec3& n) -> CubeVertex {
        return { { p.x, p.y, p.z }, { n.x, n.y, n.z }, { 0.0f, 0.0f } };
    };

    for (uint32_t axis = 0; axis < kAxisCount; ++axis) {
        const math::FVec3 dir = dirs[axis];
        const math::FVec3 a = sideA[axis];
        const math::FVec3 b = sideB[axis];

        const math::FVec3 start = starts[axis];
        const math::FVec3 end = ends[axis];
        const math::FVec3 coneBase = end - dir * coneLength;

        const uint32_t shaftBase = static_cast<uint32_t>(verts.size());
        for (uint32_t i = 0; i < kAxisSegments; ++i) {
            const float t =
                    (static_cast<float>(i) / static_cast<float>(kAxisSegments)) * 2.0f * static_cast<float>(math::kPi);
            const math::FVec3 radial = a * std::cos(t) + b * std::sin(t);
            verts.push_back(makeVert(start + radial * shaftRadius, radial));
            verts.push_back(makeVert(coneBase + radial * shaftRadius, radial));
        }

        for (uint32_t i = 0; i < kAxisSegments; ++i) {
            const uint32_t next = (i + 1) % kAxisSegments;
            const uint32_t i0 = shaftBase + i * 2;
            const uint32_t i1 = shaftBase + i * 2 + 1;
            const uint32_t n0 = shaftBase + next * 2;
            const uint32_t n1 = shaftBase + next * 2 + 1;
            indices.insert(indices.end(), { i0, i1, n1, i0, n1, n0 });
        }

        const uint32_t coneBaseIndex = static_cast<uint32_t>(verts.size());
        for (uint32_t i = 0; i < kAxisSegments; ++i) {
            const float t =
                    (static_cast<float>(i) / static_cast<float>(kAxisSegments)) * 2.0f * static_cast<float>(math::kPi);
            const math::FVec3 radial = a * std::cos(t) + b * std::sin(t);
            const math::FVec3 normal = (radial * coneLength + dir * coneRadius).normalized();
            verts.push_back(makeVert(coneBase + radial * coneRadius, normal));
        }
        const uint32_t tipIndex = static_cast<uint32_t>(verts.size());
        verts.push_back(makeVert(end, dir));

        for (uint32_t i = 0; i < kAxisSegments; ++i) {
            const uint32_t next = (i + 1) % kAxisSegments;
            indices.insert(indices.end(), { coneBaseIndex + i, tipIndex, coneBaseIndex + next });
        }
    }

    axis_index_count_ = static_cast<uint32_t>(indices.size() / kAxisCount);

    {
        auto r = device_->createBuffer(
                BufferDesc::vertex(sizeof(CubeVertex) * verts.size(), verts.data(), "ViewCube_AxisVB"));
        if (!r) {
            std::fprintf(stderr, "[ViewCube] createBuffer AxisVB: %s\n", r.error().message.c_str());
            return false;
        }
        axis_vb_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(
                BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_AxisIB"));
        if (!r) {
            std::fprintf(stderr, "[ViewCube] createBuffer AxisIB: %s\n", r.error().message.c_str());
            return false;
        }
        axis_ib_ = std::move(*r);
    }
    return true;
}

void ViewCubeStage::updateInteractionMaterials() {
    if (!material_ubo_) {
        return;
    }

    auto mixColor = [](float dst[3], const float a[3], const float b[3], float t) {
        dst[0] = a[0] * (1.0f - t) + b[0] * t;
        dst[1] = a[1] * (1.0f - t) + b[1] * t;
        dst[2] = a[2] * (1.0f - t) + b[2] * t;
    };

    const uint32_t hovered = interaction_.hasHoveredPart ? interaction_.hoveredPart.index : kPartCount;
    const uint32_t pressed = interaction_.hasPressedPart ? interaction_.pressedPart.index : kPartCount;

    for (uint32_t i = 0; i < kPartCount; ++i) {
        viewCubePartBaseColor(ViewCubeModel::parts()[i], materials_[i].baseColor);

        if (i == hovered) {
            mixColor(materials_[i].baseColor, materials_[i].baseColor, kHoverColor, 0.58f);
        }
        if (i == pressed) {
            mixColor(materials_[i].baseColor, materials_[i].baseColor, kPressedColor, 0.78f);
        }

        material_ubo_->update(i * material_stride_, sizeof(MaterialGPU), &materials_[i]);
    }
}

// ============================================================
// 渲染
// ============================================================

void ViewCubeStage::render(CommandList* cmd, const math::Mat4& mainViewMatrix, uint32_t vpWidth, uint32_t vpHeight) {
    if (!initialized_ || !cmd || !solid_pso_)
        return;

    // --- 1. 计算 ViewCube 视口（右下角）---
    const ViewCubeRect cubeRect = viewportRect(vpWidth, vpHeight);

    Viewport cubeVP{ static_cast<float>(cubeRect.x),
                     static_cast<float>(cubeRect.y),
                     static_cast<float>(cubeRect.width),
                     static_cast<float>(cubeRect.height),
                     0.0f,
                     0.1f };
    ScissorRect cubeScissor{ cubeRect.x, cubeRect.y, cubeRect.width, cubeRect.height };
    cmd->setViewport(cubeVP);
    cmd->setScissorRect(cubeScissor);

    // --- 2. 从主相机提取纯旋转 + 正交投影 ---
    math::Mat3 rotOnly = math::Mat3(mainViewMatrix);
    math::Mat4 cubeView = math::Mat4(rotOnly);
    cubeView[3] = math::Vec4(0, 0, -3.5, 1);

    double orthoSize = ViewCubeModel::kOrthoExtent;
    math::Mat4 cubeProj = math::Mat4::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1, 10.0);
    math::Mat4 corrProj = device_->clipSpaceCorrectionMatrix() * cubeProj;
    math::Mat4 cubeVP_mat = corrProj * cubeView;

    // --- 3. 上传 Scene UBO ---
    SceneUniforms sceneUbo{};
    storeGpuMat4(sceneUbo.view, cubeView);
    storeGpuMat4(sceneUbo.projection, corrProj);
    storeGpuMat4(sceneUbo.viewProjection, cubeVP_mat);
    storeGpuVec3(sceneUbo.lightDir, math::Vec3(-0.3, -1.0, -0.4));
    storeGpuVec3(sceneUbo.lightColor, math::Vec3(1.0));
    storeGpuVec3(sceneUbo.ambientColor, math::Vec3(0.85));
    storeGpuVec3(sceneUbo.edgeColor, math::Vec3(0.08, 0.08, 0.08));
    scene_ubo_->update(0, sizeof(SceneUniforms), &sceneUbo);

    // --- 4. 上传 Object UBO（单位矩阵）---
    const ObjectUniforms objUbo = makeObjectUniforms(math::Mat4(1.0));
    object_ubo_->update(0, sizeof(ObjectUniforms), &objUbo);
    updateInteractionMaterials();

    // --- 5. 渲染面（每面独立材质）---
    cmd->setPipelineState(solid_pso_);

    // 确保 face_bg_ 与当前 PSO layout 一致（PSO 不变期间复用，descriptor set 缓存生效）
    {
        const uint64_t hash = solid_pso_->bindGroupLayout().hash();
        if (!face_bg_ || face_bg_layout_hash_ != hash) {
            BindGroupDesc bg;
            bg.addUBO(0, scene_ubo_.get(), 0, sizeof(SceneUniforms))
                    .addUBO(1, object_ubo_.get(), 0, sizeof(ObjectUniforms))
                    .addUBO(2, material_ubo_.get(), 0, sizeof(MaterialGPU));
            const BindGroupLayout& layout = solid_pso_->bindGroupLayout();
            if (default_white_ && hasLayoutBinding(layout, 3)) {
                bg.addTexture(3, default_white_)
                        .addTexture(4, default_white_)
                        .addTexture(5, default_white_)
                        .addTexture(6, default_white_)
                        .addTexture(7, default_white_);
                if (default_sampler_ && hasLayoutBinding(layout, 8))
                    bg.addSampler(8, default_sampler_);
                // IBL 三件套（binding 9/10/11）：ViewCube 是 UI 元素，不需要真实 IBL，
                // 但 PSO layout 声明了这些 binding，必须填上避免 "descriptor never updated"。
                // 用 defaultWhite 占位即可（shader 采到白色，影响微乎其微）。
                if (hasLayoutBinding(layout, 9))
                    bg.addTexture(9, default_white_);
                if (hasLayoutBinding(layout, 10))
                    bg.addTexture(10, default_white_);
                if (hasLayoutBinding(layout, 11))
                    bg.addTexture(11, default_white_);
            }
            auto r = device_->createBindGroup(solid_pso_->bindGroupLayout(), bg);
            if (r) {
                face_bg_ = std::move(*r);
                face_bg_layout_hash_ = hash;
            }
        }
    }

    for (uint32_t partIndex = 0; partIndex < kPartCount; ++partIndex) {
        const uint32_t indexCount = part_index_counts_[partIndex];
        if (indexCount == 0)
            continue;

        if (face_bg_)
            face_bg_->updateUBO(2, material_ubo_.get(), partIndex * material_stride_, sizeof(MaterialGPU));
        if (face_bg_)
            cmd->bindGroup(*face_bg_);

        cmd->setVertexBuffer(0, face_vb_.get());
        cmd->setIndexBuffer(face_ib_.get());
        cmd->drawIndexed(DrawIndexedAttribs{ indexCount, 1, part_index_offsets_[partIndex] });
    }

    if (axis_vb_ && axis_ib_ && face_bg_) {
        cmd->setVertexBuffer(0, axis_vb_.get());
        cmd->setIndexBuffer(axis_ib_.get());
        for (uint32_t axis = 0; axis < kAxisCount; ++axis) {
            face_bg_->updateUBO(2, material_ubo_.get(), (kAxisMaterialOffset + axis) * material_stride_,
                                sizeof(MaterialGPU));
            cmd->bindGroup(*face_bg_);
            cmd->drawIndexed(DrawIndexedAttribs{ axis_index_count_, 1, axis * axis_index_count_ });
        }
    }

    // --- 6. 恢复全屏视口 ---
    cmd->setViewport({ 0.0f, 0.0f, static_cast<float>(vpWidth), static_cast<float>(vpHeight), 0.0f, 1.0f });
    cmd->setScissorRect({ 0, 0, static_cast<int32_t>(vpWidth), static_cast<int32_t>(vpHeight) });
}

// ============================================================
// 交互预留（空实现）
// ============================================================

bool ViewCubeStage::hitTest(int screenX, int screenY, uint32_t vpWidth, uint32_t vpHeight) const {
    // TODO: 检测屏幕坐标是否在 ViewCube 区域内
    return static_cast<bool>(pick(screenX, screenY, vpWidth, vpHeight));
}

}  // namespace mulan::engine
