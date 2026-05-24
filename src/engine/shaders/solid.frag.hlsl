/*
 * Solid 渲染 — 像素着色器
 * 双面 Lambertian + 环境光 + gamma 矫正
 * 参考 OCCT/FreeCAD 风格
 */

#include "Common.hlsli"

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

    // BaseColor 已经是线性空间（0.83 默认浅灰）
    float3 baseColorLinear = BaseColor;

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
