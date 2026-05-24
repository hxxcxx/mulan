/*
 * MSDF 文字渲染 — 顶点着色器
 *
 * 输入: pos(2f) + uv(2f) + color(packed uint)
 * 输出: 屏幕空间位置 + Atlas UV + 颜色
 *
 * @author hxxcxx
 * @date 2026-04-27
 */

cbuffer TextParams : register(b0) {
    float4x4 OrthoProjection;    // 正交投影矩阵
    float4   BgColor;            // 背景色
    float    PxRange;            // MSDF 像素范围 (通常 4.0)
    float3   _pad;
};

struct VS_INPUT {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
    uint   color    : COLOR0;    // 打包 RGBA8
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.position = mul(OrthoProjection, float4(input.position, 0.0, 1.0));
    output.texcoord = input.texcoord;

    // 解包 RGBA8 → float4
    uint c = input.color;
    output.color = float4(
        float((c >>  0) & 0xFF) / 255.0,
        float((c >>  8) & 0xFF) / 255.0,
        float((c >> 16) & 0xFF) / 255.0,
        float((c >> 24) & 0xFF) / 255.0
    );

    return output;
}
