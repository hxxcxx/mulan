/*
 * Shader 公共头 — 所有 shader 共享的常量与结构体
 *
 * Scene   (b0): 每帧更新一次 — 相机 + 多光源 + 显示设置
 * Object  (b1): 每 draw call — 物体变换 + 选中状态
 * Material(b2): 仅切换时 — 材质外观 (与 MaterialGPU 布局一致)
 */

#ifndef MULAN_GEO_COMMON_SLANG
#define MULAN_GEO_COMMON_SLANG

static const uint LIGHT_DIRECTIONAL = 0u;
static const uint LIGHT_POINT       = 1u;
static const uint LIGHT_SPOT        = 2u;
static const uint MAX_SCENE_LIGHTS  = 8u;

// 与 CPU SceneLightUniform 严格对应，共四个 16-byte 槽位。
struct SceneLight {
    float3 position;  float range;
    float3 direction; float intensity;
    float3 color;     uint type;
    float innerConeAngle;
    float outerConeAngle;
    uint castShadow;
    float _pad;
};

// ============================================================
// 场景常量（每帧更新一次, 816 bytes）
// ============================================================
[[vk::binding(0, 0)]]
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
    SceneLight Lights[MAX_SCENE_LIGHTS];
    uint LightCount;
    float Exposure;
    float2 _lightPad;
};

struct EvaluatedLight {
    float3 directionToLight;
    float3 radiance;
};

uint activeSceneLightCount() {
    return min(LightCount, MAX_SCENE_LIGHTS);
}

// 返回世界空间的“表面点指向光源”方向和已包含距离/锥角衰减的辐亮度。
EvaluatedLight evaluateSceneLight(uint index, float3 worldPosition) {
    SceneLight light = Lights[index];
    EvaluatedLight result;
    if (light.type == LIGHT_DIRECTIONAL) {
        result.directionToLight = normalize(-light.direction);
        result.radiance = light.color * max(light.intensity, 0.0);
        return result;
    }

    float3 toLight = light.position - worldPosition;
    float distanceSquared = max(dot(toLight, toLight), 1.0e-4);
    float distanceToLight = sqrt(distanceSquared);
    result.directionToLight = toLight / distanceToLight;

    // glTF punctual-light 衰减：逆平方项乘平滑 range 截止；range=0 表示无限范围。
    float attenuation = 1.0 / distanceSquared;
    if (light.range > 0.0) {
        float normalizedDistance = distanceToLight / light.range;
        float rangeFactor = saturate(1.0 - normalizedDistance * normalizedDistance *
                                           normalizedDistance * normalizedDistance);
        attenuation *= rangeFactor;
    }

    if (light.type == LIGHT_SPOT) {
        float3 spotDirection = normalize(light.direction);
        float cosTheta = dot(-result.directionToLight, spotDirection);
        float outerCos = cos(light.outerConeAngle);
        float innerCos = cos(light.innerConeAngle);
        float coneFactor = saturate((cosTheta - outerCos) / max(innerCos - outerCos, 1.0e-5));
        attenuation *= coneFactor * coneFactor;
    }

    result.radiance = light.color * max(light.intensity, 0.0) * attenuation;
    return result;
}

// ============================================================
// 物体常量（每 draw call 更新, 128 bytes）
// ============================================================
[[vk::binding(1, 0)]]
cbuffer Object : register(b1) {
    float4x4 World;
    float3x3 NormalMatrix;
    uint     PickId;
    uint     Selected;    // 0=未选中, 1=选中
    uint     Hovered;     // 0=未悬停, 1=鼠标悬停
    uint     _o0;
};

// ============================================================
// 材质常量（仅切换时更新, 80 bytes = MaterialGPU 直传）
// ============================================================
[[vk::binding(2, 0)]]
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

// 标准表面顶点: pos(3f) + normal(3f) + uv(2f) = 32 bytes
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

// Surface vertex with glTF-style tangent.xyz + bitangent sign in tangent.w.
struct VS_INPUT_TANGENT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 tangent  : TANGENT;
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

struct VS_OUTPUT_TANGENT {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 tangent  : TANGENT;
};

// wireframe / pick 用的简化输出
struct VS_OUTPUT_SIMPLE {
    float4 position : SV_POSITION;
};

#endif // MULAN_GEO_COMMON_SLANG
