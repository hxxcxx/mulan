/*
 * Shader 公共头 — 所有 shader 共享的常量与结构体
 *
 * Scene   (b0): 每帧更新一次 — 相机 + 光照 + 显示设置
 * Object  (b1): 每 draw call — 物体变换 + 选中状态
 * Material(b2): 仅切换时 — 材质外观 (与 MaterialGPU 布局一致)
 */

#ifndef MULAN_GEO_COMMON_HLSLI
#define MULAN_GEO_COMMON_HLSLI

// ============================================================
// 场景常量（每帧更新一次, 288 bytes）
// ============================================================
cbuffer Scene : register(b0) {
    float4x4 View;
    float4x4 Projection;
    float4x4 ViewProjection;
    float3   CameraPos;      float _s0;
    float3   LightDir;       float _s1;   // 主方向光方向
    float3   LightColor;     float _s2;   // 光源颜色 × 强度
    float3   AmbientColor;   float _s3;   // 环境光颜色
    float3   EdgeColor;      float _s4;   // 默认边线颜色
    float3   HighlightColor; float _s5;   // 选中高亮颜色
};

// ============================================================
// 物体常量（每 draw call 更新, 128 bytes）
// ============================================================
cbuffer Object : register(b1) {
    float4x4 World;
    float3x3 NormalMatrix;
    uint     PickId;
    uint     Selected;    // 0=未选中, 1=选中
};

// ============================================================
// 材质常量（仅切换时更新, 80 bytes = MaterialGPU 直传）
// ============================================================
cbuffer Material : register(b2) {
    float3 BaseColor;  float  Metallic;
    float3 Emissive;   float  Roughness;
    float3 Specular;   float  Shininess;
    float  Alpha;      float  AO;
    float  EmissiveStrength; float AlphaCutoff;
    uint   MaterialType; uint AlphaMode;
    uint   TextureFlags; uint DoubleSided;
};

// ============================================================
// 顶点输入 / 输出结构体
// ============================================================

// CAD 标准顶点: pos(3f) + normal(3f) + uv(2f) = 32 bytes
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

// 仅有位置的顶点（wireframe / pick）
struct VS_INPUT_POS {
    float3 position : POSITION;
};

// 顶点着色器输出
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

// wireframe / pick 用的简化输出
struct VS_OUTPUT_SIMPLE {
    float4 position : SV_POSITION;
};

#endif // MULAN_GEO_COMMON_HLSLI
