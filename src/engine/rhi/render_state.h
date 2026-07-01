/**
 * @file render_state.h
 * @brief 渲染状态枚举与结构体：光栅化、深度模板、混合
 * @author hxxcxx
 * @date 2026-04-17
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace mulan::engine {

// ============================================================
// 图元拓扑
// ============================================================

enum class PrimitiveTopology : uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,

    // 带邻接信息（用于 geometry shader）
    LineListAdj,
    LineStripAdj,
    TriangleListAdj,
    TriangleStripAdj,
};

// ============================================================
// 光栅化状态
// ============================================================

enum class CullMode : uint8_t {
    None,    // 双面渲染
    Front,
    Back,
};

enum class FrontFace : uint8_t {
    CounterClockwise,
    Clockwise,
};

enum class FillMode : uint8_t {
    Solid,
    Wireframe,
};

// ============================================================
// 深度/模板状态
// ============================================================

enum class CompareFunc : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class StencilOp : uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

struct DepthStencilOpDesc {
    StencilOp  failOp      = StencilOp::Keep;
    StencilOp  depthFailOp = StencilOp::Keep;
    StencilOp  passOp      = StencilOp::Keep;
    CompareFunc func       = CompareFunc::Always;
};

struct DepthStencilDesc {
    bool             depthEnable   = true;
    bool             depthWrite    = true;
    CompareFunc      depthFunc     = CompareFunc::LessEqual;
    bool             stencilEnable = false;
    uint8_t          stencilReadMask  = 0xFF;
    uint8_t          stencilWriteMask = 0xFF;
    DepthStencilOpDesc frontFace;
    DepthStencilOpDesc backFace;

    // 深度偏移（用于边线渲染防 z-fighting）
    float            depthBias     = 0.0f;
    float            depthBiasClamp = 0.0f;
    float            slopeScaledDepthBias = 0.0f;
};

// ============================================================
// 混合状态
// ============================================================

enum class BlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstAlpha,
    InvDstAlpha,
    DstColor,
    InvDstColor,
};

enum class BlendOp : uint8_t {
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max,
};

struct RenderTargetBlendDesc {
    bool       blendEnable   = false;
    BlendFactor srcBlend     = BlendFactor::One;
    BlendFactor dstBlend     = BlendFactor::Zero;
    BlendOp     blendOp      = BlendOp::Add;
    BlendFactor srcBlendAlpha = BlendFactor::One;
    BlendFactor dstBlendAlpha = BlendFactor::Zero;
    BlendOp     blendOpAlpha = BlendOp::Add;
    uint8_t     writeMask    = 0x0F;  // RGBA
};

struct BlendDesc {
    bool                   alphaToCoverage = false;
    bool                   independentBlend = false;
    RenderTargetBlendDesc  renderTargets[8];
};

// ============================================================
// 采样器状态
// ============================================================

enum class SamplerFilter : uint8_t {
    Nearest,
    Linear,
    Anisotropic,
};

enum class SamplerAddressMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge,
};

struct SamplerDesc {
    SamplerFilter     minFilter   = SamplerFilter::Linear;
    SamplerFilter     magFilter   = SamplerFilter::Linear;
    SamplerFilter     mipFilter   = SamplerFilter::Linear;
    SamplerAddressMode addressU   = SamplerAddressMode::Repeat;
    SamplerAddressMode addressV   = SamplerAddressMode::Repeat;
    SamplerAddressMode addressW   = SamplerAddressMode::Repeat;
    float             maxAniso    = 16.0f;
    float             mipLodBias  = 0.0f;
    float             minLod      = -1000.0f;
    float             maxLod      = 1000.0f;
    CompareFunc       compareFunc = CompareFunc::Never;
    float             borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool              anisotropyEnable = false;
    bool              compareEnable    = false;
    std::string_view  debugName;

    // 便捷构造

    static SamplerDesc linear() {
        return {};
    }

    static SamplerDesc linearClamp() {
        SamplerDesc d;
        d.addressU = SamplerAddressMode::ClampToEdge;
        d.addressV = SamplerAddressMode::ClampToEdge;
        d.addressW = SamplerAddressMode::ClampToEdge;
        return d;
    }

    static SamplerDesc point() {
        SamplerDesc d;
        d.minFilter = SamplerFilter::Nearest;
        d.magFilter = SamplerFilter::Nearest;
        d.mipFilter = SamplerFilter::Nearest;
        return d;
    }

    static SamplerDesc anisotropic(float maxAniso = 16.0f) {
        SamplerDesc d;
        d.anisotropyEnable = true;
        d.maxAniso = maxAniso;
        return d;
    }
};

} // namespace mulan::engine
