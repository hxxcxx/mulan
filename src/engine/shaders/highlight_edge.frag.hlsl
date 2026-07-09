/*
 * HighlightEdge 渲染 — 像素着色器
 * 独立绘制 hover / selected 的边线强调，不污染普通 EdgeStage。
 */

#include "common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    float3 selectedColor = lerp(EdgeColor, HighlightColor, 0.72);
    float3 color = (Hovered > 0) ? HighlightColor : selectedColor;
    return float4(color, 1.0);
}
