/*
 * MSDF 文字渲染 — 片段着色器
 *
 * 核心: median() 从 RGB 三通道提取距离 → anti-aliased Alpha
 * 渲染高质量 MSDF 文字，保持拐角锐利
 *
 * @author hxxcxx
 * @date 2026-04-27
 */

cbuffer TextParams : register(b0) {
    float4x4 OrthoProjection;
    float4   BgColor;
    float    PxRange;
    float3   _pad;
};

Texture2D    msdfAtlas : register(t0);
SamplerState sampler0  : register(s0);

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR0;
};

// MSDF 核心：从三通道距离中取中值
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float4 main(PS_INPUT input) : SV_TARGET {
    // 采样 MSDF 纹理（RGB 三通道编码距离）
    float3 msdf = msdfAtlas.Sample(sampler0, input.texcoord).rgb;

    // 提取标量距离
    float dist = median(msdf.r, msdf.g, msdf.b);

    // 将距离转换为屏幕空间像素距离
    float screenPxDistance = PxRange * (dist - 0.5);

    // Smoothstep 抗锯齿（屏幕空间约 1 像素过渡）
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    // 合成：文字颜色 × opacity + 背景色
    float4 textColor = input.color;
    float3 finalColor = lerp(BgColor.rgb, textColor.rgb, opacity);
    float  finalAlpha = textColor.a * opacity + BgColor.a * (1.0 - opacity);

    return float4(finalColor, finalAlpha);
}
