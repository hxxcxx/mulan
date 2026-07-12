/*
 * Irradiance 卷积 fragment — 漫反射 IBL（equirect 表示）
 *
 * 输出纹理是另一张 equirect：每个输出像素的法线 N 由该像素的 equirect UV 反推。
 * 对源 equirect 沿 N 的上半球做余弦加权卷积。
 *
 * 绑定:
 *   binding 1 = Source Equirect Texture2D（RGBA32F HDR）
 *   binding 2 = Linear Sampler
 *   b0        = IBLParams（SampleCount）
 */

#include "ibl_common.hlsli"

[[vk::binding(1, 0)]] Texture2D    SourceEquirect : register(t1);
[[vk::binding(2, 0)]] SamplerState LinearSamp    : register(s2);

// IBLParams UBO
cbuffer IBLParams : register(b0) {
    uint  SampleCount;
    float _p0; float _p1; float _p2;
};

// 余弦加权半球采样
float3 cosineSampleHemisphere(float2 Xi, float3 N) {
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(1.0 - Xi.y);
    float sinTheta = sqrt(Xi.y);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float4 main(VSOut IN) : SV_Target {
    // 输出像素 uv 反推为法线方向（equirect → 球面方向）
    float phi   = (IN.uv.x - 0.5) * 2.0 * PI;
    float theta = (1.0 - IN.uv.y) * PI;
    float3 N;
    N.x = sin(theta) * cos(phi);
    N.y = cos(theta);
    N.z = sin(theta) * sin(phi);
    N = normalize(N);

    float3 irradiance = float3(0.0, 0.0, 0.0);
    const uint SAMP = max(SampleCount, 1u);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMP; ++i) {
        float2 Xi = hammersley(i, SAMP);
        float3 L  = cosineSampleHemisphere(Xi, N);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            float2 luv = directionToEquirectUV(L);
            irradiance += SourceEquirect.Sample(LinearSamp, luv).rgb * NoL;
            totalWeight += NoL;
        }
    }

    if (totalWeight > 0.0) irradiance /= totalWeight;
    return float4(irradiance, 1.0);
}
