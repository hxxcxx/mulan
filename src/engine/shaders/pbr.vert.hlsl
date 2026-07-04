/*
 * PBR 渲染 — 顶点着色器
 * 标准 MVP 变换 + 世界空间位置/法线/uv + 切线
 */

#include "common.hlsli"

struct VS_OUTPUT_PBR {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 tangent  : TANGENT;
    float3 bitangent: BITANGENT;
};

VS_OUTPUT_PBR main(VS_INPUT input) {
    VS_OUTPUT_PBR output;

    float4 worldPos = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal   = normalize(mul(NormalMatrix, input.normal));
    output.texcoord = input.texcoord;

    // 切线/副切线（仅当有法线贴图时需要）
    // glTF 标准切线在 POSITION/NORMAL 之后，通过额外属性传入
    // 当前顶点布局 layouts::pbr() 包含 Tangent，通过 texcoord1 通道传入
    output.tangent   = float3(1, 0, 0);
    output.bitangent = float3(0, 1, 0);

    return output;
}
