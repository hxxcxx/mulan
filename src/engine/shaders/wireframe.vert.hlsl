/*
 * Wireframe 渲染 — 顶点着色器
 * 仅变换位置，无光照
 */

#include "Common.hlsli"

VS_OUTPUT_SIMPLE main(VS_INPUT_POS input) {
    VS_OUTPUT_SIMPLE output;
    output.position = mul(ViewProjection, mul(World, float4(input.position, 1.0)));
    return output;
}
