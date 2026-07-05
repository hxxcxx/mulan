/*
 * Prefilter 卷积 fragment — 镜面 IBL（equirect 表示，单档 roughness）
 *
 * 输出是 equirect 表示的预过滤环境贴图。固定单档 roughness（由 b0.Roughness 指定），
 * 运行时 pbr.frag.hlsl 直接按反射方向采样（不再用 mip）。
 *
 * 注：相比 cube 多 mip 方案，单档 roughness 在低/高 roughness 处略不精确，
 * 但作为简化 IBL 已远好于"用源图 LOD 凑数"。如需更高精度后续可改图集。
 *
 * 绑定:
 *   binding 1 = Source Equirect Texture2D（RGBA32F HDR）
 *   binding 2 = Linear Sampler
 *   b0        = IBLParams（SampleCount, Roughness）
 */

#include "ibl_common.hlsli"

[[vk::binding(1, 0)]] Texture2D    SourceEquirect : register(t0);
[[vk::binding(2, 0)]] SamplerState LinearSamp    : register(s0);

cbuffer IBLParams : register(b0) {
    uint  SampleCount;
    float Roughness;
    float _p0; float _p1;
};

float4 main(VSOut IN) : SV_Target {
    float phi   = (IN.uv.x - 0.5) * 2.0 * PI;
    float theta = (1.0 - IN.uv.y) * PI;
    float3 N;
    N.x = sin(theta) * cos(phi);
    N.y = cos(theta);
    N.z = sin(theta) * sin(phi);
    N = normalize(N);

    float3 V = N;  // 预过滤近似：V == N
    float3 R = N;

    const uint SAMP = max(SampleCount, 1u);
    float totalWeight = 0.0;
    float3 prefiltered = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < SAMP; ++i) {
        float2 Xi = hammersley(i, SAMP);
        float3 H  = importanceSampleGGX(Xi, N, Roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        float NoV = max(dot(N, V), 0.0);

        if (NoL > 0.0) {
            float G = geometrySmith(N, V, L, Roughness);
            float G_Vis = (G * VoH) / max(NoH * NoV, 1e-4);
            float2 luv = directionToEquirectUV(L);
            prefiltered += SourceEquirect.Sample(LinearSamp, luv).rgb * G_Vis;
            totalWeight += G_Vis;
        }
    }

    if (totalWeight > 0.0) prefiltered /= totalWeight;
    return float4(prefiltered, 1.0);
}
