/*
 * Edge 渲染 — 像素着色器
 * 根据 Hovered 标记选择边线颜色
 */

#include "common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    static const uint MaterialTypeUnlit = 0;
    float3 baseColor = (MaterialType == MaterialTypeUnlit) ? BaseColor : EdgeColor;
    float3 color = (Hovered > 0) ? lerp(baseColor, HighlightColor, 0.85) : baseColor;
    return float4(color, 1.0);
}
