/*
 * Wireframe 渲染 — 像素着色器
 * 根据 Selected 标记选择边线颜色
 */

#include "Common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    float3 color = (Selected > 0) ? HighlightColor : EdgeColor;
    return float4(color, 1.0);
}
