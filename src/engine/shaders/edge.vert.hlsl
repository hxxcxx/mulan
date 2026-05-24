/*
 * Edge 渲染 — 顶点着色器
 * 复用完整顶点布局 (pos+normal+uv)，变换位置 + 深度偏移防 z-fighting
 */

#include "Common.hlsli"

VS_OUTPUT_SIMPLE main(VS_INPUT input) {
    VS_OUTPUT_SIMPLE output;
    float4 worldPos = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);

    // 深度偏移：将边线稍微推向相机，避免与实体面 z-fighting
    // bias = -0.0005 * w 是一个经验值，在近处和远处都能有效果
    output.position.z -= 0.0005 * output.position.w;

    return output;
}
