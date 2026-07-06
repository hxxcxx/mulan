/*
 * Wireframe 渲染 — 像素着色器
 * 根据 Hovered / Selected 标记选择线框颜色
 */

#include "common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    float3 color = EdgeColor;
    if (Selected > 0) {
        color = lerp(EdgeColor, HighlightColor, 0.65);
    }
    if (Hovered > 0) {
        color = lerp(color, HighlightColor, 0.85);
    }
    return float4(color, 1.0);
}
