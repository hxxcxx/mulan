/*
 * IBL 公共头 — 共享的常量、方向转换、采样函数
 *
 * 全部基于 equirectangular 表示（不开 cube），与运行时 pbr.frag.hlsl 一致。
 */

#ifndef MULAN_IBL_COMMON_HLSLI
#define MULAN_IBL_COMMON_HLSLI

#define PI 3.14159265359

// ─── equirectangular 方向 → UV ─────────────────────────────────
float2 directionToEquirectUV(float3 dir) {
    float phi   = atan2(dir.z, dir.x);
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    return float2(0.5 + phi / (2.0 * PI), 1.0 - theta / PI);
}

// ─── 全屏三角形 VS 输出（fragment 共用） ────────────────────────
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;  // 像素 uv ∈ [0,1]
};

// ─── Van der Corput 低差异序列 ─────────────────────────────────
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverse_VdC(i));
}

// ─── GGX 几何函数（IBL 用 k = α²/8）───────────────────────────
float geometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// ─── GGX 重要度采样 ───────────────────────────────────────────
float3 importanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

#endif // MULAN_IBL_COMMON_HLSLI
