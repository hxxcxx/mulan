/*
 * Solid 渲染 — 像素着色器
 * 双面 Lambertian + 环境光 + gamma 矫正
 * 参考中性实体视图风格
 */

#include "common.slang"

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

    // 双面光照：背面翻转法线
    if (!isFrontFace) {
        N = -N;
    }

    // BaseColor 已经是线性空间（0.83 默认浅灰）
    float3 baseColorLinear = BaseColor;

    float3 directLight = float3(0.0, 0.0, 0.0);
    for (uint lightIndex = 0u; lightIndex < activeSceneLightCount(); ++lightIndex) {
        EvaluatedLight light = evaluateSceneLight(lightIndex, input.worldPos);
        float NdotL = max(dot(N, light.directionToLight), 0.0);
        directLight += baseColorLinear * light.radiance * NdotL;
    }

    // 环境光（参考项目: baseColor / PI * ambientIntensity * 3.5）
    float3 ambient = baseColorLinear * AmbientColor * 3.5;

    float3 color = (ambient + directLight) * Exposure;

    // ACES tone mapping
    color = acesTonemap(color);

    // Gamma 编码：linear → sRGB 输出
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(color, 1.0);
}
