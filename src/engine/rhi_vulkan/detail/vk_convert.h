/**
 * @file vk_convert.h
 * @brief Vulkan类型转换工具，RHI枚举到Vulkan枚举的映射
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

// 确保 Device.h 和 VkCommon.h 都已包含
#include "../../rhi/device.h"
#include "vk_common.h"

#include <mulan/graphics/vertex/vertex_format.h>

namespace mulan::engine {

using graphics::VertexFormat;

inline vk::Format toVkFormat(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::RGBA8_UNorm: return vk::Format::eR8G8B8A8Unorm;
    case TextureFormat::BGRA8_UNorm: return vk::Format::eB8G8R8A8Unorm;
    case TextureFormat::R8_UNorm: return vk::Format::eR8Unorm;
    case TextureFormat::RGBA8_sRGB: return vk::Format::eR8G8B8A8Srgb;
    case TextureFormat::BGRA8_sRGB: return vk::Format::eB8G8R8A8Srgb;
    case TextureFormat::RGBA16_Float: return vk::Format::eR16G16B16A16Sfloat;
    case TextureFormat::R16_Float: return vk::Format::eR16Sfloat;
    case TextureFormat::RG16_Float: return vk::Format::eR16G16Sfloat;
    case TextureFormat::RGBA32_Float: return vk::Format::eR32G32B32A32Sfloat;
    case TextureFormat::R32_Float: return vk::Format::eR32Sfloat;
    case TextureFormat::RG32_Float: return vk::Format::eR32G32Sfloat;
    case TextureFormat::D16_UNorm: return vk::Format::eD16Unorm;
    case TextureFormat::D24_UNorm_S8_UInt: return vk::Format::eD24UnormS8Uint;
    case TextureFormat::D32_Float: return vk::Format::eD32Sfloat;
    case TextureFormat::D32_Float_S8X24_UInt: return vk::Format::eD32SfloatS8Uint;
    default: return vk::Format::eUndefined;
    }
}

inline TextureFormat fromVkFormat(vk::Format fmt) {
    switch (fmt) {
    case vk::Format::eR8G8B8A8Unorm: return TextureFormat::RGBA8_UNorm;
    case vk::Format::eB8G8R8A8Unorm: return TextureFormat::BGRA8_UNorm;
    case vk::Format::eR8Unorm: return TextureFormat::R8_UNorm;
    case vk::Format::eR8G8B8A8Srgb: return TextureFormat::RGBA8_sRGB;
    case vk::Format::eB8G8R8A8Srgb: return TextureFormat::BGRA8_sRGB;
    case vk::Format::eR16G16B16A16Sfloat: return TextureFormat::RGBA16_Float;
    case vk::Format::eR16Sfloat: return TextureFormat::R16_Float;
    case vk::Format::eR16G16Sfloat: return TextureFormat::RG16_Float;
    case vk::Format::eR32G32B32A32Sfloat: return TextureFormat::RGBA32_Float;
    case vk::Format::eR32Sfloat: return TextureFormat::R32_Float;
    case vk::Format::eR32G32Sfloat: return TextureFormat::RG32_Float;
    case vk::Format::eD16Unorm: return TextureFormat::D16_UNorm;
    case vk::Format::eD24UnormS8Uint: return TextureFormat::D24_UNorm_S8_UInt;
    case vk::Format::eD32Sfloat: return TextureFormat::D32_Float;
    case vk::Format::eD32SfloatS8Uint: return TextureFormat::D32_Float_S8X24_UInt;
    default: return TextureFormat::Unknown;
    }
}

inline vk::SampleCountFlagBits toVkSampleCount(uint32_t count) {
    switch (count) {
    case 8: return vk::SampleCountFlagBits::e8;
    case 4: return vk::SampleCountFlagBits::e4;
    case 2: return vk::SampleCountFlagBits::e2;
    default: return vk::SampleCountFlagBits::e1;
    }
}

inline vk::PrimitiveTopology toVkTopology(PrimitiveTopology topo) {
    switch (topo) {
    case PrimitiveTopology::PointList: return vk::PrimitiveTopology::ePointList;
    case PrimitiveTopology::LineList: return vk::PrimitiveTopology::eLineList;
    case PrimitiveTopology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
    case PrimitiveTopology::TriangleList: return vk::PrimitiveTopology::eTriangleList;
    case PrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
    case PrimitiveTopology::LineListAdj: return vk::PrimitiveTopology::eLineListWithAdjacency;
    case PrimitiveTopology::LineStripAdj: return vk::PrimitiveTopology::eLineStripWithAdjacency;
    case PrimitiveTopology::TriangleListAdj: return vk::PrimitiveTopology::eTriangleListWithAdjacency;
    case PrimitiveTopology::TriangleStripAdj: return vk::PrimitiveTopology::eTriangleStripWithAdjacency;
    default: return vk::PrimitiveTopology::eTriangleList;
    }
}

inline vk::CullModeFlags toVkCullMode(CullMode mode) {
    switch (mode) {
    case CullMode::None: return vk::CullModeFlagBits::eNone;
    case CullMode::Front: return vk::CullModeFlagBits::eFront;
    case CullMode::Back: return vk::CullModeFlagBits::eBack;
    default: return vk::CullModeFlagBits::eNone;
    }
}

inline vk::FrontFace toVkFrontFace(FrontFace face) {
    return face == FrontFace::Clockwise ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
}

inline vk::PolygonMode toVkPolygonMode(FillMode mode) {
    return mode == FillMode::Wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
}

inline vk::CompareOp toVkCompareOp(CompareFunc func) {
    switch (func) {
    case CompareFunc::Never: return vk::CompareOp::eNever;
    case CompareFunc::Less: return vk::CompareOp::eLess;
    case CompareFunc::Equal: return vk::CompareOp::eEqual;
    case CompareFunc::LessEqual: return vk::CompareOp::eLessOrEqual;
    case CompareFunc::Greater: return vk::CompareOp::eGreater;
    case CompareFunc::NotEqual: return vk::CompareOp::eNotEqual;
    case CompareFunc::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
    case CompareFunc::Always: return vk::CompareOp::eAlways;
    default: return vk::CompareOp::eAlways;
    }
}

inline vk::StencilOp toVkStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep: return vk::StencilOp::eKeep;
    case StencilOp::Zero: return vk::StencilOp::eZero;
    case StencilOp::Replace: return vk::StencilOp::eReplace;
    case StencilOp::IncrementClamp: return vk::StencilOp::eIncrementAndClamp;
    case StencilOp::DecrementClamp: return vk::StencilOp::eDecrementAndClamp;
    case StencilOp::Invert: return vk::StencilOp::eInvert;
    case StencilOp::IncrementWrap: return vk::StencilOp::eIncrementAndWrap;
    case StencilOp::DecrementWrap: return vk::StencilOp::eDecrementAndWrap;
    default: return vk::StencilOp::eKeep;
    }
}

inline vk::BlendFactor toVkBlendFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return vk::BlendFactor::eZero;
    case BlendFactor::One: return vk::BlendFactor::eOne;
    case BlendFactor::SrcColor: return vk::BlendFactor::eSrcColor;
    case BlendFactor::InvSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
    case BlendFactor::SrcAlpha: return vk::BlendFactor::eSrcAlpha;
    case BlendFactor::InvSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
    case BlendFactor::DstAlpha: return vk::BlendFactor::eDstAlpha;
    case BlendFactor::InvDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
    case BlendFactor::DstColor: return vk::BlendFactor::eDstColor;
    case BlendFactor::InvDstColor: return vk::BlendFactor::eOneMinusDstColor;
    default: return vk::BlendFactor::eZero;
    }
}

inline vk::BlendOp toVkBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return vk::BlendOp::eAdd;
    case BlendOp::Subtract: return vk::BlendOp::eSubtract;
    case BlendOp::RevSubtract: return vk::BlendOp::eReverseSubtract;
    case BlendOp::Min: return vk::BlendOp::eMin;
    case BlendOp::Max: return vk::BlendOp::eMax;
    default: return vk::BlendOp::eAdd;
    }
}

inline vk::IndexType toVkIndexType(IndexType t) {
    return t == IndexType::UInt16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

inline vk::Format vertexFormatToVk(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float: return vk::Format::eR32Sfloat;
    case VertexFormat::Float2: return vk::Format::eR32G32Sfloat;
    case VertexFormat::Float3: return vk::Format::eR32G32B32Sfloat;
    case VertexFormat::Float4: return vk::Format::eR32G32B32A32Sfloat;
    case VertexFormat::UInt: return vk::Format::eR32Uint;
    case VertexFormat::UInt2: return vk::Format::eR32G32Uint;
    case VertexFormat::UInt3: return vk::Format::eR32G32B32Uint;
    case VertexFormat::UInt4: return vk::Format::eR32G32B32A32Uint;
    case VertexFormat::Int: return vk::Format::eR32Sint;
    case VertexFormat::Int2: return vk::Format::eR32G32Sint;
    case VertexFormat::Int3: return vk::Format::eR32G32B32Sint;
    case VertexFormat::Int4: return vk::Format::eR32G32B32A32Sint;
    case VertexFormat::Half2: return vk::Format::eR16G16Sfloat;
    case VertexFormat::Half4: return vk::Format::eR16G16B16A16Sfloat;
    case VertexFormat::UByte4N: return vk::Format::eR8G8B8A8Unorm;
    case VertexFormat::Byte4N: return vk::Format::eR8G8B8A8Snorm;
    case VertexFormat::RGB10A2: return vk::Format::eA2B10G10R10UnormPack32;
    case VertexFormat::RG11B10F: return vk::Format::eB10G11R11UfloatPack32;
    default: return vk::Format::eUndefined;
    }
}

// ============================================================
// LoadAction / StoreAction → VkAttachmentLoadOp / VkAttachmentStoreOp
// ============================================================

inline vk::AttachmentLoadOp toVkLoadOp(LoadAction action) {
    switch (action) {
    case LoadAction::Clear: return vk::AttachmentLoadOp::eClear;
    case LoadAction::Load: return vk::AttachmentLoadOp::eLoad;
    case LoadAction::DontCare: return vk::AttachmentLoadOp::eDontCare;
    }
    return vk::AttachmentLoadOp::eClear;
}

inline vk::AttachmentStoreOp toVkStoreOp(StoreAction action) {
    switch (action) {
    case StoreAction::Store: return vk::AttachmentStoreOp::eStore;
    case StoreAction::DontCare: return vk::AttachmentStoreOp::eDontCare;
    }
    return vk::AttachmentStoreOp::eStore;
}

}  // namespace mulan::engine
