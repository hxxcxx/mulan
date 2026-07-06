/*
 * Edge 渲染 — 像素着色器
 * 根据 Hovered 标记选择边线颜色
 */

#include "common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    float3 color = (Hovered > 0) ? lerp(EdgeColor, HighlightColor, 0.85) : EdgeColor;
    return float4(color, 1.0);
}
