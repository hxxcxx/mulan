/*
 * PBR 渲染 — 像素着色器
 * Metallic-Roughness 工作流 (glTF 2.0 标准)
 *
 * 纹理绑定:
 *   binding 3 = Albedo (t3)      — baseColor × sRGB texture
 *   binding 4 = Normal (t4)      — tangent-space normal map
 *   binding 5 = MetallicRoughness(t5) — B=metal, G=roughness
 *   binding 6 = Emissive (t6)    — HDR emissive
 *   binding 7 = AO (t7)          — ambient occlusion
 *   binding 7 = AO (t7)          — ambient occlusion
 *   binding 8 = Sampler (s4)     — shared linear-repeat sampler
 *   binding 9 = EnvMap (t9)      — equirect HDR environment map (IBL)
 *
 * TextureFlags bitmask (来自 Material cbuffer):
 *   bit0 = albedo, bit1 = normal, bit2 = mr, bit3 = emissive, bit4 = ao
 */

#include "common.hlsli"

[[vk::binding(3, 0)]] Texture2D    AlbedoTex    : register(t3);
[[vk::binding(4, 0)]] Texture2D    NormalTex    : register(t4);
[[vk::binding(5, 0)]] Texture2D    MRTex        : register(t5);
[[vk::binding(6, 0)]] Texture2D    EmissiveTex  : register(t6);
[[vk::binding(7, 0)]] Texture2D    AOTex        : register(t7);
[[vk::binding(8, 0)]] SamplerState PbrSampler   : register(s4);
[[vk::binding(9, 0)]] Texture2D    EnvMap       : register(t9);

#define TF_ALBEDO   0x01u
#define TF_NORMAL   0x02u
#define TF_MR       0x04u
#define TF_EMISSIVE 0x08u
#define TF_AO       0x10u

static const float PI = 3.14159265359;

// ─── PBR functions ────────────────────────────────────────────

float3 schlickFresnel(float3 f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float ggxDistribution(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float smithGeometry(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float ggx1 = NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
    float ggx2 = NdotL / max(NdotL * (1.0 - k) + k, 1e-5);
    return ggx1 * ggx2;
}

// ─── 切线空间法线 ─────────────────────────────────────────────

float3 perturbNormal(float3 N, float3 V, float2 uv, float3 tangent, float3 bitangent) {
    float3 tn = NormalTex.Sample(PbrSampler, uv).rgb * 2.0 - 1.0;
    float3 T = normalize(tangent - N * dot(tangent, N));
    float3 B = cross(N, T);
    return normalize(tn.x * T + tn.y * B + tn.z * N);
}

// ─── IBL: equirect UV from direction ─────────────────────────

float2 directionToEquirectUV(float3 dir) {
    float phi   = atan2(dir.z, dir.x);
    float theta = acos(dir.y);
    return float2(0.5 + phi / (2.0 * PI), 1.0 - theta / PI);
}

float3 sampleEnvMap(float3 dir, float lod) {
    float2 uv = directionToEquirectUV(dir);
    return EnvMap.SampleLevel(PbrSampler, uv, lod).rgb;
}

// ─── ACES tonemap ─────────────────────────────────────────────

float3 acesTonemap(float3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─── Main ─────────────────────────────────────────────────────

float4 main(VS_OUTPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(CameraPos - input.worldPos);

    // 双面光照
    if (!isFrontFace) N = -N;

    // ── 纹理采样 ──
    uint flags = TextureFlags;

    float3 albedo = (flags & TF_ALBEDO)
        ? AlbedoTex.Sample(PbrSampler, input.texcoord).rgb
        : float3(1, 1, 1);
    albedo *= BaseColor;

    // 法线贴图（简化版：无切线时跳过 TBN）
    if (flags & TF_NORMAL) {
        float3 tn = NormalTex.Sample(PbrSampler, input.texcoord).rgb * 2.0 - 1.0;
        N = normalize(N + tn.x * normalize(cross(N, float3(0,1,0)))
                       + tn.y * normalize(cross(float3(0,1,0), N)));
    }

    float metallic  = (flags & TF_MR) ? MRTex.Sample(PbrSampler, input.texcoord).b : Metallic;
    float roughness = (flags & TF_MR) ? MRTex.Sample(PbrSampler, input.texcoord).g : Roughness;
    roughness = max(roughness, 0.04);

    float ao = (flags & TF_AO) ? AOTex.Sample(PbrSampler, input.texcoord).r : AO;

    float3 emissive = Emissive;
    if (flags & TF_EMISSIVE)
        emissive *= EmissiveTex.Sample(PbrSampler, input.texcoord).rgb;

    // ── 光照计算 ──
    float3 L = normalize(-LightDir);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-5);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Dielectric F0 (non-metal ≈ 0.04)
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 F  = schlickFresnel(f0, max(VdotH, 0.0));
    float D  = ggxDistribution(NdotH, roughness);
    float G  = smithGeometry(NdotV, NdotL, roughness);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-5);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    float3 directLight = (diffuse + specular) * LightColor * NdotL;

    // ── IBL 环境光照 ──
    float3 R = reflect(-V, N);

    // Diffuse IBL: 法线方向采样（低 LOD ≈ 模糊预过滤）
    float3 irradiance = sampleEnvMap(N, 4.0);
    float3 diffuseIBL = irradiance * kD * albedo * ao;

    // Specular IBL: 反射方向采样（LOD 按 roughness）
    float specLod = roughness * 4.0;
    float3 specularIBL = sampleEnvMap(R, specLod) * F * ao;

    float3 iblScale = AmbientColor * 3.5; // 保持与之前环境光强度兼容
    float3 ambient = (diffuseIBL + specularIBL) * iblScale;

    float3 color = ambient + directLight + emissive;

    color = acesTonemap(color);
    color = pow(color, 1.0 / 2.2);

    return float4(color, Alpha);
}
