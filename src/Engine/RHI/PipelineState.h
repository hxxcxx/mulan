/**
 * @file PipelineState.h
 * @brief 渲染管线状态集与接口定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "RenderState.h"
#include "Shader.h"
#include "Texture.h"
#include "VertexLayout.h"

#include <cstdint>
#include <string_view>

namespace MulanGeo::engine {

// ============================================================
// 输入布局元素（PSO 层定义 stride）
// ============================================================

struct InputLayoutElement {
    uint32_t    inputSlot     = 0;   // vertex buffer slot
    uint32_t    stride        = 0;   // 该 slot 的顶点 stride
    uint32_t    instanceDataStepRate = 0;  // 0 = per-vertex
};

// ============================================================
// 图形管线描述结构体
// ============================================================

enum class DescriptorType : uint8_t {
    UniformBuffer,
    TextureSRV,
    Sampler,
};

struct PipelineBinding {
    uint32_t       binding = 0;
    uint32_t       count   = 1;
    DescriptorType type    = DescriptorType::UniformBuffer;

    static constexpr uint32_t kStageVertex   = 0x00000001;
    static constexpr uint32_t kStageFragment = 0x00000010;
    static constexpr uint32_t kStageAll      = 0x7FFFFFFF;

    uint32_t       stages  = kStageAll;
};

struct GraphicsPipelineDesc {
    std::string_view name;

    // 着色器
    Shader* vs = nullptr;   // Vertex
    Shader* ps = nullptr;   // Pixel / Fragment
    Shader* gs = nullptr;   // Geometry（可选）
    Shader* cs = nullptr;   // Compute（仅计算管线）

    // Descriptor bindings — 描述着色器需要的资源绑定
    static constexpr uint8_t kMaxDescriptorBindings = 16;
    PipelineBinding  descriptorBindings[kMaxDescriptorBindings] = {};
    uint8_t          descriptorBindingCount = 0;

    // 输入布局 — 关联到 VertexLayout
    VertexLayout        vertexLayout;
    PrimitiveTopology   topology   = PrimitiveTopology::TriangleList;

    // 光栅化
    CullMode   cullMode   = CullMode::Back;
    FrontFace  frontFace  = FrontFace::CounterClockwise;
    FillMode   fillMode   = FillMode::Solid;

    // 深度/模板
    DepthStencilDesc depthStencil;

    // 混合
    BlendDesc blend;

    // 渲染目标格式 — 创建时必须提供
    static constexpr uint8_t kMaxRenderTargets = 8;
    TextureFormat  colorFormats[kMaxRenderTargets] = {};
    uint8_t        colorTargetCount   = 0;
    TextureFormat  depthStencilFormat = TextureFormat::Unknown;
    bool           depthEnable        = false;
    uint32_t       sampleCount        = 1;   // MSAA: 1=无, 2/4/8=多采样
};

// ============================================================
// 管线状态基类
//
// 涵盖一次绘制所需的全部粗粒度状态。
// 由 Device::createPipelineState() 一步创建完成，无需 finalize。
// ============================================================

class PipelineState {
public:
    virtual ~PipelineState() = default;

    virtual const GraphicsPipelineDesc& desc() const = 0;

    // 便捷查询
    PrimitiveTopology topology() const { return desc().topology; }

protected:
    PipelineState() = default;
    PipelineState(const PipelineState&) = delete;
    PipelineState& operator=(const PipelineState&) = delete;
};

} // namespace MulanGeo::Engine
