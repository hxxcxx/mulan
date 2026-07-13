/*
 * BRDF LUT 积分 fragment
 *
 * 输出 RG = (scale, bias)，运行时
 *   specularIBL = prefiltered * (F0 * scale + bias)
 * 输出 RG16_Float，512×512，UV → (NdotV, roughness)。
 *
 * 绑定: b0 = IBLParams（SampleCount）
 */

#include "ibl_common.slang"

[[vk::binding(0, 0)]]
cbuffer IBLParams : register(b0) {
    uint  SampleCount;
    float _p0; float _p1; float _p2;
};

float2 integrateBRDF(float NdotV, float roughness) {
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    const uint SAMP = max(SampleCount, 1u);
    float3 N = float3(0.0, 0.0, 1.0);

    for (uint i = 0u; i < SAMP; ++i) {
        float2 Xi = hammersley(i, SAMP);
        float3 H  = importanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);
        float NoV = max(V.z, 0.0);

        if (NoL > 0.0) {
            float G = geometrySmith(N, V, L, roughness);
            float G_Vis = (G * VoH) / max(NoH * NoV, 1e-4);
            float Fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / float(SAMP);
}

float4 main(VSOut IN) : SV_Target {
    float NdotV     = clamp(IN.uv.x, 0.0, 1.0);
    float roughness = clamp(IN.uv.y, 0.0, 1.0);
    float2 rg       = integrateBRDF(NdotV, roughness);
    return float4(rg, 0.0, 1.0);
}
