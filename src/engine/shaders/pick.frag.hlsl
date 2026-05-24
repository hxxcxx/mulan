/*
 * Pick 拾取 — 像素着色器
 * 将 pickId 编码为颜色输出（R = id, GBA = 0）
 */

#include "Common.hlsli"

struct PS_OUTPUT {
    float4 color : SV_TARGET;
};

PS_OUTPUT main(VS_OUTPUT_SIMPLE input) {
    PS_OUTPUT output;
    // pickId 编码到 R 通道（8位足够 255 个物体，后续可扩展为 24 位）
    uint r = (PickId >>  0) & 0xFF;
    uint g = (PickId >>  8) & 0xFF;
    uint b = (PickId >> 16) & 0xFF;
    output.color = float4(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0, 1.0);
    return output;
}
