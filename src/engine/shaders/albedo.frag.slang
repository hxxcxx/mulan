/*
 * Albedo 渲染 — 像素着色器
 *
 * 在 solid.frag 基础上加 albedo 纹理采样：
 *   baseColorLinear = BaseColor * AlbedoTex.Sample(...)
 *
 * 无材质模型绑定默认 1×1 白纹理 → sampled=(1,1,1) → 退化为纯色，
 * 与改造前 solid.frag 数学等价，保证零回归。
 *
 * binding 约定（与 GeometryDrawExecutor PSO descriptorBindings 一致）：
 *   binding 0/1/2 = Scene/Object/Material UBO（隐式 set 0）
 *   binding 3 = TextureSRV (albedo)
 *   binding 4 = Sampler
 *
 * 注：HLSL register(t3)/register(s4) 与 binding 编号一致；
 *     [[vk::binding(N,0)]] 显式锁定 Vulkan binding，不依赖隐式映射。
 */

#include "common.slang"
#include "shader_compat.slang"

#ifdef MULAN_OPENGL
[[vk::binding(3, 0)]] Sampler2D AlbedoTex;
#else
[[vk::binding(3, 0)]] Texture2D    AlbedoTex     : register(t3);
[[vk::binding(4, 0)]] SamplerState AlbedoSampler : register(s4);
#endif

// ACES filmic tone mapping
float3 acesTonemap(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float4 main(VS_OUTPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 L = normalize(-LightDir);

    // 双面光照：背面翻转法线
    if (!isFrontFace) {
        N = -N;
    }

    float NdotL = max(dot(N, L), 0.0);

    // BaseColor × albedo 纹理（白纹理时 sampled=(1,1,1) → 等价纯色）
    float3 sampled = MULAN_SAMPLE_2D(AlbedoTex, AlbedoSampler, input.texcoord).rgb;
    float3 baseColorLinear = BaseColor * sampled;

    // 主光漫反射（乘 LightColor 参考项目风格）
    float3 diffuse = baseColorLinear * LightColor * NdotL;

    // 环境光（参考项目: baseColor / PI * ambientIntensity * 3.5）
    float3 ambient = baseColorLinear * AmbientColor * 3.5;

    float3 color = ambient + diffuse;

    // ACES tone mapping
    color = acesTonemap(color);

    // Gamma 编码：linear → sRGB 输出
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, 1.0);
}
