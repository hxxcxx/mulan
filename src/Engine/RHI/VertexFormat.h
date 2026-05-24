/**
 * @file VertexFormat.h
 * @brief 顶点数据格式枚举与特征定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <cstdint>
#include <array>
#include <string_view>

namespace MulanGeo::engine {

// ============================================================
// 顶点分量格式
// ============================================================

enum class VertexFormat : uint8_t {
    Invalid    = 0,

    // --- Float ---
    Float      = 1,   // 1 x f32
    Float2     = 2,   // 2 x f32
    Float3     = 3,   // 3 x f32
    Float4     = 4,   // 4 x f32

    // --- Half ---
    Half2      = 5,   // 2 x f16
    Half4      = 6,   // 4 x f16

    // --- SNorm ---
    SNorm2     = 7,   // 2 x i16 → [-1, 1]
    SNorm4     = 8,   // 4 x i16 → [-1, 1]
    Byte4N     = 9,   // 4 x i8  → [-1, 1]

    // --- UNorm ---
    UNorm2     = 10,  // 2 x u16 → [0, 1]
    UNorm4     = 11,  // 4 x u16 → [0, 1]
    UByte4N    = 12,  // 4 x u8  → [0, 1]

    // --- Integer ---
    Int        = 13,  // 1 x i32
    Int2       = 14,  // 2 x i32
    Int3       = 15,  // 3 x i32
    Int4       = 16,  // 4 x i32
    UInt       = 17,  // 1 x u32
    UInt2      = 18,  // 2 x u32
    UInt3      = 19,  // 3 x u32
    UInt4      = 20,  // 4 x u32
    UByte4     = 21,  // 4 x u8 (raw)

    // --- Packed ---
    RGB10A2    = 22,  // 10-10-10-2 packed, unorm
    RG11B10F   = 23,  // 11-11-10 packed, float

    Count
};

// ============================================================
// 格式特征信息（全部 constexpr）
// ============================================================

struct VertexFormatInfo {
    VertexFormat format;
    uint8_t      componentCount;  // 标量分量数
    uint8_t      bytesPerComponent;
    uint8_t      totalBytes;
    bool         isNormalized;
    bool         isFloat;
    bool         isInteger;
    bool         isPacked;
    const char*  name;
    const char*  hlslType;
    const char*  glslType;
};

constexpr VertexFormatInfo getVertexFormatInfo(VertexFormat fmt) {
    using enum VertexFormat;
    switch (fmt) {
        //                    fmt       cnt  bpc  tot  norm  flt  int  pack  name          hlsl         glsl
        case Invalid:    return {fmt,     0,   0,   0,  false,false,false,false,"invalid",    "void",      "void"};
        case Float:      return {fmt,     1,   4,   4,  false,true, false,false,"float",      "float",     "float"};
        case Float2:     return {fmt,     2,   4,   8,  false,true, false,false,"float2",     "float2",    "vec2"};
        case Float3:     return {fmt,     3,   4,  12,  false,true, false,false,"float3",     "float3",    "vec3"};
        case Float4:     return {fmt,     4,   4,  16,  false,true, false,false,"float4",     "float4",    "vec4"};
        case Half2:      return {fmt,     2,   2,   4,  false,true, false,false,"half2",      "float2",    "vec2"};
        case Half4:      return {fmt,     4,   2,   8,  false,true, false,false,"half4",      "float4",    "vec4"};
        case SNorm2:     return {fmt,     2,   2,   4,  true, false,false,false,"snorm2",     "float2",    "vec2"};
        case SNorm4:     return {fmt,     4,   2,   8,  true, false,false,false,"snorm4",     "float4",    "vec4"};
        case Byte4N:     return {fmt,     4,   1,   4,  true, false,false,false,"byte4n",     "float4",    "vec4"};
        case UNorm2:     return {fmt,     2,   2,   4,  true, false,false,false,"unorm2",     "float2",    "vec2"};
        case UNorm4:     return {fmt,     4,   2,   8,  true, false,false,false,"unorm4",     "float4",    "vec4"};
        case UByte4N:    return {fmt,     4,   1,   4,  true, false,false,false,"ubyte4n",    "float4",    "vec4"};
        case Int:        return {fmt,     1,   4,   4,  false,false,true, false,"int",        "int",       "int"};
        case Int2:       return {fmt,     2,   4,   8,  false,false,true, false,"int2",       "int2",      "ivec2"};
        case Int3:       return {fmt,     3,   4,  12,  false,false,true, false,"int3",       "int3",      "ivec3"};
        case Int4:       return {fmt,     4,   4,  16,  false,false,true, false,"int4",       "int4",      "ivec4"};
        case UInt:       return {fmt,     1,   4,   4,  false,false,true, false,"uint",       "uint",      "uint"};
        case UInt2:      return {fmt,     2,   4,   8,  false,false,true, false,"uint2",      "uint2",     "uvec2"};
        case UInt3:      return {fmt,     3,   4,  12,  false,false,true, false,"uint3",      "uint3",     "uvec3"};
        case UInt4:      return {fmt,     4,   4,  16,  false,false,true, false,"uint4",      "uint4",     "uvec4"};
        case UByte4:     return {fmt,     4,   1,   4,  false,false,true, false,"ubyte4",     "uint4",     "uvec4"};
        case RGB10A2:    return {fmt,     4,   4,   4,  true, false,false,true, "rgb10a2",    "float4",    "vec4"};
        case RG11B10F:   return {fmt,     3,   4,   4,  false,true, false,true, "rg11b10f",   "float3",    "vec3"};
        default:         return {Invalid, 0,   0,   0,  false,false,false,false,"unknown",    "void",      "void"};
    }
}

// 快捷查询
constexpr uint8_t vertexFormatSize(VertexFormat fmt) {
    return getVertexFormatInfo(fmt).totalBytes;
}

constexpr const char* vertexFormatName(VertexFormat fmt) {
    return getVertexFormatInfo(fmt).name;
}

// ============================================================
// VertexFormat → C++ 类型映射（供 VertexElement 使用）
// ============================================================

template<VertexFormat F>
struct VertexFormatTraits;

template<> struct VertexFormatTraits<VertexFormat::Float>   { using type = float; };
template<> struct VertexFormatTraits<VertexFormat::UInt>    { using type = uint32_t; };
template<> struct VertexFormatTraits<VertexFormat::UByte4N> { using type = uint32_t; };
template<> struct VertexFormatTraits<VertexFormat::Int>     { using type = int32_t; };

template<> struct VertexFormatTraits<VertexFormat::Float2> {
    struct type { float x, y; };
};
template<> struct VertexFormatTraits<VertexFormat::Float3> {
    struct type { float x, y, z; };
};
template<> struct VertexFormatTraits<VertexFormat::Float4> {
    struct type { float x, y, z, w; };
};
template<> struct VertexFormatTraits<VertexFormat::Half2> {
    struct type { uint16_t x, y; };
};
template<> struct VertexFormatTraits<VertexFormat::Half4> {
    struct type { uint16_t x, y, z, w; };
};
template<> struct VertexFormatTraits<VertexFormat::UInt2> {
    struct type { uint32_t x, y; };
};
template<> struct VertexFormatTraits<VertexFormat::UInt3> {
    struct type { uint32_t x, y, z; };
};
template<> struct VertexFormatTraits<VertexFormat::UInt4> {
    struct type { uint32_t x, y, z, w; };
};
template<> struct VertexFormatTraits<VertexFormat::Int2> {
    struct type { int32_t x, y; };
};
template<> struct VertexFormatTraits<VertexFormat::Int3> {
    struct type { int32_t x, y, z; };
};
template<> struct VertexFormatTraits<VertexFormat::Int4> {
    struct type { int32_t x, y, z, w; };
};
template<> struct VertexFormatTraits<VertexFormat::UByte4> {
    struct type { uint8_t x, y, z, w; };
};

// 未特化的格式，回退为原始字节数组
template<VertexFormat F>
struct VertexFormatTraits {
    using type = std::array<std::byte, vertexFormatSize(F)>;
};

} // namespace MulanGeo::Engine
