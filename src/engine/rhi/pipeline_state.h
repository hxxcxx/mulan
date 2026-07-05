/**
 * @file pipeline_state.h
 * @brief 渲染管线状态集与接口定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "render_state.h"
#include "shader.h"
#include "texture.h"

#include <mulan/graphics/vertex/vertex_layout.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace mulan::engine {

class BindGroupLayout;

using graphics::VertexLayout;

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
    static constexpr uint32_t kStageCompute  = 0x00000100;
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

    // Push constant / root constant 范围
    // 若 size > 0，则 PSO 创建时会预留该范围
    uint32_t       pushConstantSize   = 0;   // 字节，必须 ≤ 128（VK 保证的最小值）
};

// ============================================================
// Compute 管线描述 — 仅 cs + descriptor bindings + push constant
// ============================================================

struct ComputePipelineDesc {
    std::string_view name;

    Shader* cs = nullptr;

    static constexpr uint8_t kMaxDescriptorBindings = 16;
    PipelineBinding  descriptorBindings[kMaxDescriptorBindings] = {};
    uint8_t          descriptorBindingCount = 0;

    uint32_t       pushConstantSize = 0;
};

// ============================================================
// 管线状态基类
// ============================================================

class PipelineState {
public:
    virtual ~PipelineState();

    virtual const GraphicsPipelineDesc& desc() const = 0;

    // 便捷查询
    PrimitiveTopology topology() const { return desc().topology; }

    /// 此 PSO 的 BindGroup 契约（从 descriptorBindings 派生，惰性缓存）。
    /// 后端无关、内容等价的 PSO 派生出的 layout 哈希一致，便于 BindGroup 跨 PSO 复用。
    const BindGroupLayout& bindGroupLayout() const;

protected:
    PipelineState();
    PipelineState(const PipelineState&) = delete;
    PipelineState& operator=(const PipelineState&) = delete;

private:
    // bindGroupLayout() 惰性缓存（不可变 desc → 派生结果稳定，mutable 允许 const 计算）
    mutable std::unique_ptr<BindGroupLayout> bg_layout_;
};

// ============================================================
// Compute 管线状态基类
// ============================================================

class ComputePipelineState {
public:
    virtual ~ComputePipelineState() = default;

    virtual const ComputePipelineDesc& desc() const = 0;

protected:
    ComputePipelineState() = default;
    ComputePipelineState(const ComputePipelineState&) = delete;
    ComputePipelineState& operator=(const ComputePipelineState&) = delete;
};

} // namespace mulan::engine
