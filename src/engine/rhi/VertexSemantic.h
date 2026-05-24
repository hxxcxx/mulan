/**
 * @file VertexSemantic.h
 * @brief 顶点语义槽位定义，描述顶点属性含义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

// ============================================================
// 顶点语义
// ============================================================

enum class VertexSemantic : uint8_t {
    Position    = 0,   // float3  - 物体空间 position
    Normal      = 1,   // float3  - 物体空间 normal
    Tangent     = 2,   // float4  - tangent + handedness(w)
    Bitangent   = 3,   // float3  - binormal
    Color0      = 4,   // ubyte4n / float4 - 主颜色
    Color1      = 5,   // ubyte4n / float4 - 副颜色
    TexCoord0   = 6,   // float2  - UV 通道 0
    TexCoord1   = 7,   // float2  - UV 通道 1
    TexCoord2   = 8,   // float2  - UV 通道 2
    TexCoord3   = 9,   // float2  - UV 通道 3
    Indices     = 10,  // uint4   - bone / instance 索引
    Weight      = 11,  // float4  - bone 权重
    InstanceId  = 12,  // uint    - 系统 instance ID
    PickId      = 13,  // uint    - picking 用对象 ID
    LayerId     = 14,  // uint    - CAD 图层 ID
    ObjectId    = 15,  // uint    - 场景对象 ID
    MaterialId  = 16,  // uint    - material 索引
    User0       = 17,  // float4  - 自定义
    User1       = 18,  // float4  - 自定义
    User2       = 19,  // float4  - 自定义
    User3       = 20,  // float4  - 自定义
    Count
};

// 语义名称
constexpr const char* semanticName(VertexSemantic sem) {
    using enum VertexSemantic;
    switch (sem) {
        case Position:   return "POSITION";
        case Normal:     return "NORMAL";
        case Tangent:    return "TANGENT";
        case Bitangent:  return "BITANGENT";
        case Color0:     return "COLOR0";
        case Color1:     return "COLOR1";
        case TexCoord0:  return "TEXCOORD0";
        case TexCoord1:  return "TEXCOORD1";
        case TexCoord2:  return "TEXCOORD2";
        case TexCoord3:  return "TEXCOORD3";
        case Indices:    return "INDICES";
        case Weight:     return "WEIGHT";
        case InstanceId: return "INSTANCE_ID";
        case PickId:     return "PICK_ID";
        case LayerId:    return "LAYER_ID";
        case ObjectId:   return "OBJECT_ID";
        case MaterialId: return "MATERIAL_ID";
        case User0:      return "USER0";
        case User1:      return "USER1";
        case User2:      return "USER2";
        case User3:      return "USER3";
        default:         return "UNKNOWN";
    }
}

// HLSL 语义名称（着色器生成用）
constexpr const char* semanticHlsl(VertexSemantic sem) {
    using enum VertexSemantic;
    switch (sem) {
        case Position:   return "POSITION";
        case Normal:     return "NORMAL";
        case Tangent:    return "TANGENT";
        case Bitangent:  return "BINORMAL";
        case Color0:     return "COLOR";
        case Color1:     return "COLOR1";
        case TexCoord0:  return "TEXCOORD";
        case TexCoord1:  return "TEXCOORD1";
        case TexCoord2:  return "TEXCOORD2";
        case TexCoord3:  return "TEXCOORD3";
        case Indices:    return "BLENDINDICES";
        case Weight:     return "BLENDWEIGHT";
        case InstanceId: return "SV_InstanceID";
        case PickId:     return "PICK_ID";
        case LayerId:    return "LAYER_ID";
        case ObjectId:   return "OBJECT_ID";
        case MaterialId: return "MATERIAL_ID";
        default:         return "TEXCOORD";
    }
}

// GLSL 变量前缀
constexpr const char* semanticGlsl(VertexSemantic sem) {
    using enum VertexSemantic;
    switch (sem) {
        case Position:   return "a_position";
        case Normal:     return "a_normal";
        case Tangent:    return "a_tangent";
        case Bitangent:  return "a_bitangent";
        case Color0:     return "a_color0";
        case Color1:     return "a_color1";
        case TexCoord0:  return "a_texcoord0";
        case TexCoord1:  return "a_texcoord1";
        case TexCoord2:  return "a_texcoord2";
        case TexCoord3:  return "a_texcoord3";
        case Indices:    return "a_indices";
        case Weight:     return "a_weight";
        case InstanceId: return "gl_InstanceID";
        case PickId:     return "a_pickId";
        case LayerId:    return "a_layerId";
        case ObjectId:   return "a_objectId";
        case MaterialId: return "a_materialId";
        case User0:      return "a_user0";
        case User1:      return "a_user1";
        case User2:      return "a_user2";
        case User3:      return "a_user3";
        default:         return "a_user";
    }
}

} // namespace mulan::Engine
