#include "view_cube_stage.h"
#include "../text/text_types.h"
#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../rhi/engine_error_code.h"
#include "../gpu_scene_contract.h"

#include <mulan/core/result/error.h>
#include <mulan/core/log/log.h>
#include <mulan/graphics/vertex/vertex_layout.h>
#include <mulan/math/math.h>

#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

namespace mulan::engine {

namespace {

bool hasLayoutBinding(const BindGroupLayout& layout, uint32_t binding) {
    for (const auto& entry : layout.entries()) {
        if (entry.binding == binding) {
            return true;
        }
    }
    return false;
}

const char* viewCubeFaceLabel(const ViewCubePart& part) {
    if (part.z > 0)
        return "TOP";
    if (part.z < 0)
        return "BOTTOM";
    if (part.x < 0)
        return "LEFT";
    if (part.x > 0)
        return "RIGHT";
    if (part.y > 0)
        return "BACK";
    return "FRONT";
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void viewCubeFaceTextAxes(const ViewCubePart& part, math::Vec3& right, math::Vec3& up) {
    if (part.z != 0) {
        right = math::Vec3(static_cast<double>(part.z), 0.0, 0.0);
        up = math::Vec3(0.0, 1.0, 0.0);
        return;
    }
    if (part.x != 0) {
        right = math::Vec3(0.0, 0.0, static_cast<double>(-part.x));
        up = math::Vec3(0.0, 1.0, 0.0);
        return;
    }
    right = math::Vec3(1.0, 0.0, 0.0);
    up = math::Vec3(0.0, 0.0, static_cast<double>(-part.y));
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
    initialized_ = false;
}

// ============================================================
// 初始化
// ============================================================

ResultVoid ViewCubeStage::init(RHIDevice& device, const RenderTargetInfo&) {
    device_ = &device;

    // --- 初始化材质 ---
    material_.roughness = 0.8f;
    material_.alpha = 1.0f;
    material_.ao = 1.0f;

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
    ViewCubeLayout layout = layout_;
    layout.size = size;
    layout_ = layout;
}

void ViewCubeStage::setMargin(uint32_t margin) {
    ViewCubeLayout layout = layout_;
    layout.margin = margin;
    layout_ = layout;
}

void ViewCubeStage::setLayout(const ViewCubeLayout& layout) {
    layout_ = layout;
}

void ViewCubeStage::setInteraction(const ViewCubeInteractionState& interaction) {
    const bool changed =
            interaction_.hasHoveredPart != interaction.hasHoveredPart ||
            interaction_.hasPressedPart != interaction.hasPressedPart ||
            (interaction.hasHoveredPart && interaction_.hoveredPart.index != interaction.hoveredPart.index) ||
            (interaction.hasPressedPart && interaction_.pressedPart.index != interaction.pressedPart.index);
    interaction_ = interaction;
    interaction_geometry_dirty_ = interaction_geometry_dirty_ || changed;
}

void ViewCubeStage::setCorner(ViewCubeCorner corner) {
    ViewCubeLayout layout = layout_;
    layout.corner = corner;
    layout_ = layout;
}

void ViewCubeStage::collectLabels(TextDrawList& textDraws, const math::Mat4& mainViewMatrix, uint32_t vpWidth,
                                  uint32_t vpHeight) const {
    if (!initialized_) {
        return;
    }

    const ViewCubeRect cubeRect = layout_.rect(vpWidth, vpHeight);
    if (cubeRect.width <= 0 || cubeRect.height <= 0) {
        return;
    }

    const math::Mat3 rotOnly(mainViewMatrix);
    math::Mat4 cubeView(rotOnly);
    cubeView[3] = math::Vec4(0, 0, -ViewCubeStyle::ViewDistance, 1);
    const double orthoSize = ViewCubeStyle::OrthoExtent;
    const math::Mat4 cubeProj = math::Mat4::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1, 10.0);
    const math::Mat4 cubeVP = cubeProj * cubeView;

    for (const auto& part : ViewCubeGeometry::parts()) {
        if (part.type != ViewCubePartType::Face) {
            continue;
        }

        const math::Vec3 normal = ViewCubeGeometry::partNormal(part);
        const math::Vec3 center = normal * (ViewCubeStyle::CubeHalfExtent + ViewCubeStyle::LabelSurfaceOffset);
        const math::Vec3 viewNormal = rotOnly * normal;
        const math::Vec4 viewCenter4 = cubeView * math::Vec4(center, 1.0);
        const math::Vec3 viewCenter(viewCenter4.x, viewCenter4.y, viewCenter4.z);
        const math::Vec3 toCamera = (-viewCenter).normalizedOr(math::Vec3::unitZ());
        const float facing = static_cast<float>(viewNormal.dot(toCamera));
        const float alpha = smoothstep(ViewCubeStyle::LabelFacingFadeStart, ViewCubeStyle::LabelFacingFadeEnd, facing);
        if (alpha <= 0.01f) {
            continue;
        }

        math::Vec3 faceRight;
        math::Vec3 faceUp;
        viewCubeFaceTextAxes(part, faceRight, faceUp);

        textDraws.add(TextDrawDesc::worldPlanar(
                viewCubeFaceLabel(part), math::Point3(center), faceRight, faceUp, cubeVP,
                math::Point2(static_cast<double>(cubeRect.x), static_cast<double>(cubeRect.y)),
                math::Vec2(static_cast<double>(cubeRect.width), static_cast<double>(cubeRect.height)),
                ViewCubeStyle::LabelSizePx, ViewCubeStyle::LabelSizeWorld, math::Vec4(0.05, 0.055, 0.06, alpha)));
    }
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
    verts.reserve(ViewCubeGeometry::kPartCount * 4);
    indices.reserve(ViewCubeGeometry::kPartCount * 6);
    part_vertex_offsets_.fill(0);
    part_vertex_counts_.fill(0);

    auto makeVert = [](const math::Vec3& p, const math::Vec3& n, const float color[3]) -> CubeVertex {
        CubeVertex result{};
        result.pos[0] = static_cast<float>(p.x);
        result.pos[1] = static_cast<float>(p.y);
        result.pos[2] = static_cast<float>(p.z);
        result.normal[0] = static_cast<float>(n.x);
        result.normal[1] = static_cast<float>(n.y);
        result.normal[2] = static_cast<float>(n.z);
        for (size_t channel = 0; channel < 3; ++channel)
            result.color[channel] = static_cast<uint8_t>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
        result.color[3] = 255;
        return result;
    };

    for (const auto& part : ViewCubeGeometry::parts()) {
        const ViewCubePartShape shape = ViewCubeGeometry::partShape(part);
        if (shape.vertexCount < 3 || shape.vertexCount > 4)
            continue;

        const math::Vec3 normal = ViewCubeGeometry::partNormal(part);
        const uint32_t base = static_cast<uint32_t>(verts.size());
        float color[3]{};
        viewCubePartBaseColor(part, color);

        for (uint32_t i = 0; i < shape.vertexCount; ++i) {
            verts.push_back(makeVert(shape.vertices[i], normal, color));
        }

        if (shape.vertexCount == 3) {
            indices.insert(indices.end(), { base, base + 1, base + 2 });
        } else {
            indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }

        part_vertex_offsets_[part.index] = base;
        part_vertex_counts_[part.index] = shape.vertexCount;
    }

    face_index_count_ = static_cast<uint32_t>(indices.size());

    face_vertices_ = verts;
    {
        auto r = device_->createBuffer(BufferDesc::dynamicVertex(
                static_cast<uint32_t>(sizeof(CubeVertex) * face_vertices_.size()), "ViewCube_FaceVB"));
        if (!r) {
            LOG_ERROR("[ViewCube] Face vertex-buffer creation failed: {}", r.error().message);
            return false;
        }
        face_vb_ = std::move(*r);
        const auto written = face_vb_->write(0, static_cast<uint32_t>(sizeof(CubeVertex) * face_vertices_.size()),
                                             face_vertices_.data());
        if (!written) {
            LOG_ERROR("[ViewCube] Face vertex-buffer upload failed: {}", written.error().message);
            return false;
        }
    }
    {
        auto r = device_->createBuffer(
                BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_FaceIB"));
        if (!r) {
            LOG_ERROR("[ViewCube] Face index-buffer creation failed: {}", r.error().message);
            return false;
        }
        face_ib_ = std::move(*r);
    }
    return true;
}

bool ViewCubeStage::createAxisGeometry() {
    constexpr float frameMin = ViewCubeStyle::AxisOrigin;
    constexpr float frameMax = ViewCubeStyle::AxisEnd;
    constexpr float coneLength = ViewCubeStyle::AxisConeLength;
    constexpr float shaftRadius = ViewCubeStyle::AxisShaftRadius;
    constexpr float coneRadius = ViewCubeStyle::AxisConeRadius;

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

    auto makeVert = [](const math::FVec3& p, const math::FVec3& n, const float color[3]) -> CubeVertex {
        CubeVertex result{};
        result.pos[0] = p.x;
        result.pos[1] = p.y;
        result.pos[2] = p.z;
        result.normal[0] = n.x;
        result.normal[1] = n.y;
        result.normal[2] = n.z;
        for (size_t channel = 0; channel < 3; ++channel)
            result.color[channel] = static_cast<uint8_t>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
        result.color[3] = 255;
        return result;
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
            verts.push_back(makeVert(start + radial * shaftRadius, radial, kAxisColors[axis]));
            verts.push_back(makeVert(coneBase + radial * shaftRadius, radial, kAxisColors[axis]));
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
            verts.push_back(makeVert(coneBase + radial * coneRadius, normal, kAxisColors[axis]));
        }
        const uint32_t tipIndex = static_cast<uint32_t>(verts.size());
        verts.push_back(makeVert(end, dir, kAxisColors[axis]));

        for (uint32_t i = 0; i < kAxisSegments; ++i) {
            const uint32_t next = (i + 1) % kAxisSegments;
            indices.insert(indices.end(), { coneBaseIndex + i, tipIndex, coneBaseIndex + next });
        }
    }

    axis_index_count_ = static_cast<uint32_t>(indices.size());

    {
        auto r = device_->createBuffer(
                BufferDesc::vertex(sizeof(CubeVertex) * verts.size(), verts.data(), "ViewCube_AxisVB"));
        if (!r) {
            LOG_ERROR("[ViewCube] Axis vertex-buffer creation failed: {}", r.error().message);
            return false;
        }
        axis_vb_ = std::move(*r);
    }
    {
        auto r = device_->createBuffer(
                BufferDesc::index(sizeof(uint32_t) * indices.size(), indices.data(), "ViewCube_AxisIB"));
        if (!r) {
            LOG_ERROR("[ViewCube] Axis index-buffer creation failed: {}", r.error().message);
            return false;
        }
        axis_ib_ = std::move(*r);
    }
    return true;
}

bool ViewCubeStage::updateInteractionGeometry() {
    auto mixColor = [](float dst[3], const float a[3], const float b[3], float t) {
        dst[0] = a[0] * (1.0f - t) + b[0] * t;
        dst[1] = a[1] * (1.0f - t) + b[1] * t;
        dst[2] = a[2] * (1.0f - t) + b[2] * t;
    };

    const uint32_t hovered = interaction_.hasHoveredPart ? interaction_.hoveredPart.index : kPartCount;
    const uint32_t pressed = interaction_.hasPressedPart ? interaction_.pressedPart.index : kPartCount;

    for (uint32_t i = 0; i < kPartCount; ++i) {
        float color[3]{};
        viewCubePartBaseColor(ViewCubeGeometry::parts()[i], color);

        if (i == hovered) {
            mixColor(color, color, kHoverColor, 0.58f);
        }
        if (i == pressed) {
            mixColor(color, color, kPressedColor, 0.78f);
        }
        const uint32_t first = part_vertex_offsets_[i];
        const uint32_t count = part_vertex_counts_[i];
        for (uint32_t vertex = first; vertex < first + count; ++vertex) {
            for (size_t channel = 0; channel < 3; ++channel) {
                face_vertices_[vertex].color[channel] =
                        static_cast<uint8_t>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    const auto written = face_vb_->write(0, static_cast<uint32_t>(sizeof(CubeVertex) * face_vertices_.size()),
                                         face_vertices_.data());
    if (!written) {
        LOG_ERROR("[ViewCube] Interaction vertex upload failed: {}", written.error().message);
        return false;
    }
    interaction_geometry_dirty_ = false;
    return true;
}

// ============================================================
// 渲染
// ============================================================

void ViewCubeStage::render(CommandList* cmd, const math::Mat4& mainViewMatrix, uint32_t vpWidth, uint32_t vpHeight) {
    if (!initialized_ || !cmd || !solid_pso_)
        return;

    // --- 1. 计算 ViewCube 视口（右下角）---
    const ViewCubeRect cubeRect = layout_.rect(vpWidth, vpHeight);

    Viewport cubeVP{ static_cast<float>(cubeRect.x),
                     static_cast<float>(cubeRect.y),
                     static_cast<float>(cubeRect.width),
                     static_cast<float>(cubeRect.height),
                     0.0f,
                     0.1f };
    ScissorRect cubeScissor{ cubeRect.x, cubeRect.y, cubeRect.width, cubeRect.height };
    cmd->setViewport(cubeVP);
    cmd->setScissorRect(cubeScissor);
    const auto restoreFullViewport = [&] {
        cmd->setViewport({ 0.0f, 0.0f, static_cast<float>(vpWidth), static_cast<float>(vpHeight), 0.0f, 1.0f });
        cmd->setScissorRect({ 0, 0, static_cast<int32_t>(vpWidth), static_cast<int32_t>(vpHeight) });
    };

    // --- 2. 从主相机提取纯旋转 + 正交投影 ---
    math::Mat3 rotOnly = math::Mat3(mainViewMatrix);
    math::Mat4 cubeView = math::Mat4(rotOnly);
    cubeView[3] = math::Vec4(0, 0, -ViewCubeStyle::ViewDistance, 1);

    double orthoSize = ViewCubeStyle::OrthoExtent;
    math::Mat4 cubeProj = math::Mat4::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1, 10.0);
    math::Mat4 corrProj = device_->clipSpaceCorrectionMatrix() * cubeProj;
    math::Mat4 cubeVP_mat = corrProj * cubeView;

    // --- 3. 写入场景 Uniform ---
    SceneUniforms sceneUbo{};
    storeGpuMat4(sceneUbo.view, cubeView);
    storeGpuMat4(sceneUbo.projection, corrProj);
    storeGpuMat4(sceneUbo.viewProjection, cubeVP_mat);
    const math::Vec3 viewLightDir = math::Vec3(0.0, 0.0, -1.0);
    const math::Vec3 cubeLightDir = (rotOnly.transposed() * viewLightDir).normalizedOr(math::Vec3::unitZ());
    storeGpuVec3(sceneUbo.lightDir, cubeLightDir);
    storeGpuVec3(sceneUbo.lightColor, math::Vec3(1.0));
    storeGpuVec3(sceneUbo.ambientColor, math::Vec3(0.85));
    storeGpuVec3(sceneUbo.edgeColor, math::Vec3(0.08, 0.08, 0.08));
    const auto sceneUniform = cmd->writeUniform(sceneUbo);
    if (!sceneUniform) {
        restoreFullViewport();
        return;
    }

    // --- 4. 写入对象 Uniform（单位矩阵）---
    const ObjectUniforms objUbo = makeObjectUniforms(math::Mat4(1.0));
    const auto objectUniform = cmd->writeUniform(objUbo);
    if (!objectUniform) {
        restoreFullViewport();
        return;
    }
    if (interaction_geometry_dirty_ && !updateInteractionGeometry()) {
        restoreFullViewport();
        return;
    }

    // --- 5. 顶点颜色合批：26 个部件一次 draw，3 根轴一次 draw ---
    cmd->setPipelineState(solid_pso_);

    // 确保 face_bg_ 与当前 PSO layout 一致（PSO 不变期间复用，descriptor set 缓存生效）
    {
        const uint64_t hash = solid_pso_->bindGroupLayout().hash();
        if (!face_bg_ || face_bg_layout_hash_ != hash) {
            BindGroupDesc bg;
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

    const auto materialUniform = cmd->writeUniform(material_);
    if (materialUniform && face_bg_) {
        const std::array uniforms{ DynamicUniformBinding{ 0, *sceneUniform },
                                   DynamicUniformBinding{ 1, *objectUniform },
                                   DynamicUniformBinding{ 2, *materialUniform } };
        cmd->bindGroup(*face_bg_, uniforms);
        if (face_vb_ && face_ib_ && face_index_count_ > 0) {
            cmd->setVertexBuffer(0, face_vb_.get());
            cmd->setIndexBuffer(face_ib_.get());
            cmd->drawIndexed(DrawIndexedAttribs{ face_index_count_, 1, 0 });
        }

        if (axis_vb_ && axis_ib_ && axis_index_count_ > 0) {
            cmd->setVertexBuffer(0, axis_vb_.get());
            cmd->setIndexBuffer(axis_ib_.get());
            cmd->drawIndexed(DrawIndexedAttribs{ axis_index_count_, 1, 0 });
        }
    }

    // --- 6. 恢复全屏视口 ---
    restoreFullViewport();
}

}  // namespace mulan::engine
