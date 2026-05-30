/**
 * @file DX12Convert.h
 * @brief D3D12 类型转换工具，RHI 枚举到 DX12 枚举的映射
 * @author hxxcxx
 * @date 2026-04-18
 */

#pragma once

#include "../Device.h"
#include "DX12Common.h"

namespace mulan::engine {

// ============================================================
// TextureFormat → DXGI_FORMAT
// ============================================================

inline DXGI_FORMAT toDXGIFormat(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8_UNorm:         return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::BGRA8_UNorm:         return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R8_UNorm:            return DXGI_FORMAT_R8_UNORM;
        case TextureFormat::RGBA8_sRGB:          return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8_sRGB:          return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case TextureFormat::RGBA16_Float:        return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R16_Float:           return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::RGBA32_Float:        return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::R32_Float:           return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RG32_Float:          return DXGI_FORMAT_R32G32_FLOAT;
        case TextureFormat::D16_UNorm:           return DXGI_FORMAT_D16_UNORM;
        case TextureFormat::D24_UNorm_S8_UInt:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32_Float:           return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::D32_Float_S8X24_UInt:return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case TextureFormat::BC1_RGBA_UNorm:      return DXGI_FORMAT_BC1_UNORM;
        case TextureFormat::BC3_RGBA_UNorm:      return DXGI_FORMAT_BC3_UNORM;
        case TextureFormat::BC5_RG_UNorm:        return DXGI_FORMAT_BC5_UNORM;
        case TextureFormat::BC7_RGBA_UNorm:      return DXGI_FORMAT_BC7_UNORM;
        case TextureFormat::BC7_RGBA_sRGB:       return DXGI_FORMAT_BC7_UNORM_SRGB;
        default:                                 return DXGI_FORMAT_UNKNOWN;
    }
}

/// 获取 DepthStencil 视图格式（typeless → typed）
inline DXGI_FORMAT toDSVFormat(TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::D16_UNorm:           return DXGI_FORMAT_D16_UNORM;
        case TextureFormat::D24_UNorm_S8_UInt:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32_Float:           return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::D32_Float_S8X24_UInt:return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:                                 return toDXGIFormat(fmt);
    }
}

/// 获取 SRV 的 typeless → typed 格式
inline DXGI_FORMAT toSRVFormat(TextureFormat fmt) {
    // Depth 纹理作为 SRV 读取时使用可读格式
    switch (fmt) {
        case TextureFormat::D16_UNorm:           return DXGI_FORMAT_R16_UNORM;
        case TextureFormat::D24_UNorm_S8_UInt:   return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case TextureFormat::D32_Float:           return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::D32_Float_S8X24_UInt:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:                                 return toDXGIFormat(fmt);
    }
}

// ============================================================
// PrimitiveTopology → D3D12_PRIMITIVE_TOPOLOGY
// ============================================================

inline D3D12_PRIMITIVE_TOPOLOGY toDX12Topology(PrimitiveTopology topo) {
    switch (topo) {
        case PrimitiveTopology::PointList:       return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveTopology::LineList:        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveTopology::LineStrip:       return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PrimitiveTopology::TriangleList:    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case PrimitiveTopology::TriangleStrip:   return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case PrimitiveTopology::LineListAdj:     return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        case PrimitiveTopology::LineStripAdj:    return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
        case PrimitiveTopology::TriangleListAdj: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        case PrimitiveTopology::TriangleStripAdj:return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        default:                                 return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// ============================================================
// Blend / Depth / Rasterizer → D3D12
// ============================================================

inline D3D12_BLEND toDX12Blend(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:             return D3D12_BLEND_ZERO;
        case BlendFactor::One:              return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor:         return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:      return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:         return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:      return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha:         return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha:      return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::DstColor:         return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::InvDstColor:      return D3D12_BLEND_INV_DEST_COLOR;
        default:                            return D3D12_BLEND_ZERO;
    }
}

inline D3D12_BLEND_OP toDX12BlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:              return D3D12_BLEND_OP_ADD;
        case BlendOp::Subtract:         return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract:      return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min:              return D3D12_BLEND_OP_MIN;
        case BlendOp::Max:              return D3D12_BLEND_OP_MAX;
        default:                        return D3D12_BLEND_OP_ADD;
    }
}

inline D3D12_COMPARISON_FUNC toDX12CompareFunc(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never:         return D3D12_COMPARISON_FUNC_NEVER;
        case CompareFunc::Less:          return D3D12_COMPARISON_FUNC_LESS;
        case CompareFunc::Equal:         return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareFunc::LessEqual:     return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareFunc::Greater:       return D3D12_COMPARISON_FUNC_GREATER;
        case CompareFunc::NotEqual:      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareFunc::GreaterEqual:  return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareFunc::Always:        return D3D12_COMPARISON_FUNC_ALWAYS;
        default:                         return D3D12_COMPARISON_FUNC_NEVER;
    }
}

inline D3D12_CULL_MODE toDX12CullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None:   return D3D12_CULL_MODE_NONE;
        case CullMode::Front:  return D3D12_CULL_MODE_FRONT;
        case CullMode::Back:   return D3D12_CULL_MODE_BACK;
        default:               return D3D12_CULL_MODE_BACK;
    }
}

inline D3D12_FILL_MODE toDX12FillMode(FillMode mode) {
    switch (mode) {
        case FillMode::Solid:     return D3D12_FILL_MODE_SOLID;
        case FillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
        default:                  return D3D12_FILL_MODE_SOLID;
    }
}

inline D3D12_STENCIL_OP toDX12StencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::Keep:           return D3D12_STENCIL_OP_KEEP;
        case StencilOp::Zero:           return D3D12_STENCIL_OP_ZERO;
        case StencilOp::Replace:        return D3D12_STENCIL_OP_REPLACE;
        case StencilOp::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
        case StencilOp::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
        case StencilOp::Invert:         return D3D12_STENCIL_OP_INVERT;
        case StencilOp::IncrementWrap:  return D3D12_STENCIL_OP_INCR;
        case StencilOp::DecrementWrap:  return D3D12_STENCIL_OP_DECR;
        default:                        return D3D12_STENCIL_OP_KEEP;
    }
}

// ============================================================
// ResourceState → D3D12_RESOURCE_STATES
// ============================================================

inline D3D12_RESOURCE_STATES toDX12ResourceStates(ResourceState state) {
    switch (state) {
        case ResourceState::Common:          return D3D12_RESOURCE_STATE_COMMON;
        case ResourceState::VertexBuffer:    return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case ResourceState::IndexBuffer:     return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case ResourceState::UniformBuffer:   return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case ResourceState::ShaderResource:  return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                    | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case ResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case ResourceState::RenderTarget:    return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::DepthWrite:      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case ResourceState::DepthRead:       return D3D12_RESOURCE_STATE_DEPTH_READ;
        case ResourceState::Present:         return D3D12_RESOURCE_STATE_PRESENT;
        case ResourceState::CopyDest:        return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::CopySrc:         return D3D12_RESOURCE_STATE_COPY_SOURCE;
        default:                             return D3D12_RESOURCE_STATE_COMMON;
    }
}

// ============================================================
// VertexFormat → DXGI_FORMAT
// ============================================================

inline DXGI_FORMAT toDXGIFormat(VertexFormat fmt) {
    switch (fmt) {
        case VertexFormat::Float:    return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2:   return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3:   return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UInt:     return DXGI_FORMAT_R32_UINT;
        case VertexFormat::UInt2:    return DXGI_FORMAT_R32G32_UINT;
        case VertexFormat::UInt3:    return DXGI_FORMAT_R32G32B32_UINT;
        case VertexFormat::UInt4:    return DXGI_FORMAT_R32G32B32A32_UINT;
        default:                     return DXGI_FORMAT_UNKNOWN;
    }
}

} // namespace mulan::engine
