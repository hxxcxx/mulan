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
 *   binding 8 = Sampler (s8)     — shared linear sampler
 *   binding 9 = IrradianceMap (t9)  — Texture2D equirect, diffuse IBL
 *   binding 10 = PrefilterMap (t10) — Texture2D equirect, specular IBL
 *   binding 11 = BrdfLUT      (t11) — Texture2D RG, split-sum BRDF 积分
 *
 * TextureFlags bitmask (来自 Material cbuffer):
 *   bit0 = albedo, bit1 = normal, bit2 = mr, bit3 = emissive, bit4 = ao
 */

#include "common.slang"
#include "shader_compat.slang"

#ifdef MULAN_OPENGL
[[vk::binding(3, 0)]]  Sampler2D AlbedoTex;
[[vk::binding(4, 0)]]  Sampler2D NormalTex;
[[vk::binding(5, 0)]]  Sampler2D MRTex;
[[vk::binding(6, 0)]]  Sampler2D EmissiveTex;
[[vk::binding(7, 0)]]  Sampler2D AOTex;
[[vk::binding(9, 0)]]  Sampler2D IrradianceMap;
[[vk::binding(10, 0)]] Sampler2D PrefilterMap;
[[vk::binding(11, 0)]] Sampler2D BrdfLUT;
#else
[[vk::binding(3, 0)]]  Texture2D    AlbedoTex     : register(t3);
[[vk::binding(4, 0)]]  Texture2D    NormalTex     : register(t4);
[[vk::binding(5, 0)]]  Texture2D    MRTex         : register(t5);
[[vk::binding(6, 0)]]  Texture2D    EmissiveTex   : register(t6);
[[vk::binding(7, 0)]]  Texture2D    AOTex         : register(t7);
[[vk::binding(8, 0)]]  SamplerState PbrSampler    : register(s8);
[[vk::binding(9, 0)]]  Texture2D    IrradianceMap : register(t9);
[[vk::binding(10, 0)]] Texture2D    PrefilterMap  : register(t10);
[[vk::binding(11, 0)]] Texture2D    BrdfLUT       : register(t11);
#endif

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

float3 schlickFresnelRoughness(float3 f0, float cosTheta, float roughness) {
    float3 grazing = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0);
    return f0 + (grazing - f0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
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

float3 perturbNormal(float3 N, float3 worldPos, float2 uv) {
    float3 tn = MULAN_SAMPLE_2D(NormalTex, PbrSampler, uv).rgb * 2.0 - 1.0;
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1e-8) {
        return N;
    }

    float3 T = duv2.y * dp1 - duv1.y * dp2;
    float3 B = -duv2.x * dp1 + duv1.x * dp2;

    T = normalize(T - N * dot(N, T));
    B = normalize(B - N * dot(N, B));
    if (dot(cross(T, B), N) < 0.0) {
        B = -B;
    }

    return normalize(tn.x * T + tn.y * B + tn.z * N);
}

#ifdef MULAN_USE_VERTEX_TANGENT
float3 perturbNormal(float3 N, float2 uv, float4 tangent) {
    float3 tn = MULAN_SAMPLE_2D(NormalTex, PbrSampler, uv).rgb * 2.0 - 1.0;
    float3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    float3 B = normalize(cross(N, T) * tangent.w);
    return normalize(tn.x * T + tn.y * B + tn.z * N);
}
#endif

// ─── IBL: 已烘焙的 irradiance/prefilter equirect + BRDF LUT ──────
// irradiance/prefilter 用 equirect 表示，需把方向转回 uv（与烘焙时一致）。
float2 directionToEquirectUV(float3 dir) {
    float phi   = atan2(dir.z, dir.x);
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    return float2(0.5 + phi / (2.0 * PI), 1.0 - theta / PI);
}

// ─── ACES tonemap ─────────────────────────────────────────────

float3 acesTonemap(float3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─── Main ─────────────────────────────────────────────────────

#ifdef MULAN_USE_VERTEX_TANGENT
float4 main(VS_OUTPUT_TANGENT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
#else
float4 main(VS_OUTPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
#endif
    float3 N = normalize(input.normal);
    float3 V = normalize(CameraPos - input.worldPos);

    // 双面光照
    if (!isFrontFace) N = -N;

    // ── 纹理采样 ──
    uint flags = TextureFlags;

    float3 albedo = ((flags & TF_ALBEDO) != 0u)
        ? MULAN_SAMPLE_2D(AlbedoTex, PbrSampler, input.texcoord).rgb
        : float3(1, 1, 1);
    albedo *= BaseColor;

    if ((flags & TF_NORMAL) != 0u) {
#ifdef MULAN_USE_VERTEX_TANGENT
        N = perturbNormal(N, input.texcoord, input.tangent);
#else
        // Fallback path for surface meshes that do not carry tangent data.
        N = perturbNormal(N, input.worldPos, input.texcoord);
#endif
    }

    float metallic  = ((flags & TF_MR) != 0u) ? MULAN_SAMPLE_2D(MRTex, PbrSampler, input.texcoord).b : Metallic;
    float roughness = ((flags & TF_MR) != 0u) ? MULAN_SAMPLE_2D(MRTex, PbrSampler, input.texcoord).g : Roughness;
    roughness = max(roughness, 0.04);

    float ao = ((flags & TF_AO) != 0u) ? MULAN_SAMPLE_2D(AOTex, PbrSampler, input.texcoord).r : AO;

    float3 emissive = Emissive;
    if ((flags & TF_EMISSIVE) != 0u)
        emissive *= MULAN_SAMPLE_2D(EmissiveTex, PbrSampler, input.texcoord).rgb;

    // ── 光照计算 ──
    float NdotV = max(dot(N, V), 1e-5);

    // Dielectric F0 (non-metal ≈ 0.04)
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    // IBL Fresnel 只依赖视线与表面，不能被直接灯数量、顺序或位置改变。
    float3 FEnvironment = schlickFresnelRoughness(f0, NdotV, roughness);
    float3 directLight = float3(0.0, 0.0, 0.0);
    for (uint lightIndex = 0u; lightIndex < activeSceneLightCount(); ++lightIndex) {
        EvaluatedLight light = evaluateSceneLight(lightIndex, input.worldPos);
        float3 L = light.directionToLight;
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float3 FDirect = schlickFresnel(f0, VdotH);
        float D = ggxDistribution(NdotH, roughness);
        float G = smithGeometry(NdotV, NdotL, roughness);
        float3 specular = (D * G * FDirect) / max(4.0 * NdotV * NdotL, 1e-5);
        float3 kDDirect = (1.0 - FDirect) * (1.0 - metallic);
        float3 diffuse = kDDirect * albedo / PI;
        directLight += (diffuse + specular) * light.radiance * NdotL;
    }
    float3 kD = (1.0 - FEnvironment) * (1.0 - metallic);

    // ── IBL 环境光照（split-sum 近似，equirect 表示）──
    float3 R = reflect(-V, N);

    // Diffuse IBL: irradiance equirect 沿法线采样（HDR 时为离线卷积，默认时为内置环境光照）。
    // 未启用 HDR 时，binding 9 使用内置默认环境，避免背光面陷入纯黑。
    float3 irradiance = MULAN_SAMPLE_2D(IrradianceMap, PbrSampler, directionToEquirectUV(N)).rgb;
    float3 diffuseIBL = irradiance * kD * albedo * ao;

    // Specular IBL: prefilter equirect 沿反射方向采样（单档 roughness，运行时不再按 mip 选）。
    // 未启用 HDR 时，binding 10 同样提供默认环境反射。
    float3 prefiltered = MULAN_SAMPLE_2D(PrefilterMap, PbrSampler, directionToEquirectUV(R)).rgb;
    // BRDF LUT: 输入 (NdotV, roughness)，输出 (scale, bias)
    float2 brdf = MULAN_SAMPLE_2D(BrdfLUT, PbrSampler, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefiltered * (FEnvironment * brdf.x + brdf.y) * ao;

    // 基础环境光：与 IBL 无关的常数项，保证工程视图的背光面仍有最低可见度。
    // kD * albedo：让基础环境光也走 PBR 漫反射（金属几乎无漫反射，符合物理）。
    float3 baseAmbient = kD * albedo * ao;

    float3 ambient = (baseAmbient + diffuseIBL + specularIBL) * AmbientColor;

    float3 color = (ambient + directLight + emissive) * Exposure;

    color = acesTonemap(color);
    color = pow(color, 1.0 / 2.2);

    return float4(color, Alpha);
}
