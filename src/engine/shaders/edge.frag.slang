/*
 * Edge 渲染 — 像素着色器
 * 仅绘制普通边线颜色；hover / selected 高亮由 HighlightStage 独立绘制。
 */

#include "common.slang"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    static const uint ShadingModelUnlit = 0;
    float3 color = (ShadingModel == ShadingModelUnlit) ? BaseColor : EdgeColor;
    return float4(color, 1.0);
}
