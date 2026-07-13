/*
 * HighlightSurface 渲染 — 像素着色器
 * 独立绘制选中对象的半透明填充覆盖，不参与普通材质着色。
 */

#include "common.slang"

float4 main() : SV_TARGET {
    const float alpha = (Selected > 0) ? 0.30 : 0.0;
    return float4(HighlightColor, alpha);
}
