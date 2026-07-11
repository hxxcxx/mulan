/**
 * @file dx11_convert.h
 * @brief D3D11 类型转换工具，RHI 枚举到 D3D11 枚举的映射
 * @author zmb
 * @date 2026-04-19
 */

#pragma once

#include "../../rhi/device.h"
#include "dx11_common.h"

namespace mulan::engine {

using graphics::VertexFormat;

// ============================================================
// TextureFormat → DXGI_FORMAT
// ============================================================

inline DXGI_FORMAT toDXGIFormat11(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::RGBA8_sRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::BGRA8_sRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case TextureFormat::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::R16_Float: return DXGI_FORMAT_R16_FLOAT;
    case TextureFormat::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::RG32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case TextureFormat::D16_UNorm: return DXGI_FORMAT_D16_UNORM;
    case TextureFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case TextureFormat::D32_Float_S8X24_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case TextureFormat::BC1_RGBA_UNorm: return DXGI_FORMAT_BC1_UNORM;
    case TextureFormat::BC3_RGBA_UNorm: return DXGI_FORMAT_BC3_UNORM;
    case TextureFormat::BC5_RG_UNorm: return DXGI_FORMAT_BC5_UNORM;
    case TextureFormat::BC7_RGBA_UNorm: return DXGI_FORMAT_BC7_UNORM;
    case TextureFormat::BC7_RGBA_sRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

inline DXGI_FORMAT toDSVFormat11(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::D16_UNorm: return DXGI_FORMAT_D16_UNORM;
    case TextureFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case TextureFormat::D32_Float_S8X24_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default: return toDXGIFormat11(fmt);
    }
}

inline DXGI_FORMAT toTypelessFormat11(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::D16_UNorm: return DXGI_FORMAT_R16_TYPELESS;
    case TextureFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
    case TextureFormat::D32_Float: return DXGI_FORMAT_R32_TYPELESS;
    case TextureFormat::D32_Float_S8X24_UInt: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default: return toDXGIFormat11(fmt);
    }
}

// ============================================================
// PrimitiveTopology → D3D11_PRIMITIVE_TOPOLOGY
// ============================================================

inline D3D11_PRIMITIVE_TOPOLOGY toDX11Topology(PrimitiveTopology topo) {
    switch (topo) {
    case PrimitiveTopology::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::LineListAdj: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    case PrimitiveTopology::LineStripAdj: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
    case PrimitiveTopology::TriangleListAdj: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
    case PrimitiveTopology::TriangleStripAdj: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// ============================================================
// Blend / Depth / Rasterizer → D3D11
// ============================================================

inline D3D11_BLEND toDX11Blend(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return D3D11_BLEND_ZERO;
    case BlendFactor::One: return D3D11_BLEND_ONE;
    case BlendFactor::SrcColor: return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DstColor: return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::InvDstColor: return D3D11_BLEND_INV_DEST_COLOR;
    default: return D3D11_BLEND_ZERO;
    }
}

inline D3D11_BLEND_OP toDX11BlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract: return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min: return D3D11_BLEND_OP_MIN;
    case BlendOp::Max: return D3D11_BLEND_OP_MAX;
    default: return D3D11_BLEND_OP_ADD;
    }
}

inline D3D11_COMPARISON_FUNC toDX11CompareFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less: return D3D11_COMPARISON_LESS;
    case CompareFunc::Equal: return D3D11_COMPARISON_EQUAL;
    case CompareFunc::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Greater: return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Always: return D3D11_COMPARISON_ALWAYS;
    default: return D3D11_COMPARISON_NEVER;
    }
}

inline D3D11_CULL_MODE toDX11CullMode(CullMode mode) {
    switch (mode) {
    case CullMode::None: return D3D11_CULL_NONE;
    case CullMode::Front: return D3D11_CULL_FRONT;
    case CullMode::Back: return D3D11_CULL_BACK;
    default: return D3D11_CULL_BACK;
    }
}

inline D3D11_FILL_MODE toDX11FillMode(FillMode mode) {
    switch (mode) {
    case FillMode::Solid: return D3D11_FILL_SOLID;
    case FillMode::Wireframe: return D3D11_FILL_WIREFRAME;
    default: return D3D11_FILL_SOLID;
    }
}

inline D3D11_STENCIL_OP toDX11StencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep: return D3D11_STENCIL_OP_KEEP;
    case StencilOp::Zero: return D3D11_STENCIL_OP_ZERO;
    case StencilOp::Replace: return D3D11_STENCIL_OP_REPLACE;
    case StencilOp::IncrementClamp: return D3D11_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementClamp: return D3D11_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert: return D3D11_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap: return D3D11_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap: return D3D11_STENCIL_OP_DECR;
    default: return D3D11_STENCIL_OP_KEEP;
    }
}

// ============================================================
// VertexFormat → DXGI_FORMAT
// ============================================================

inline DXGI_FORMAT toDXGIFormat11(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float: return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::UInt: return DXGI_FORMAT_R32_UINT;
    case VertexFormat::UInt2: return DXGI_FORMAT_R32G32_UINT;
    case VertexFormat::UInt3: return DXGI_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt4: return DXGI_FORMAT_R32G32B32A32_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

}  // namespace mulan::engine
