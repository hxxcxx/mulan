/*
 * Wireframe 渲染 — 像素着色器
 * 仅绘制普通线框颜色；hover / selected 高亮由 HighlightStage 独立绘制。
 */

#include "common.slang"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    return float4(EdgeColor, 1.0);
}
