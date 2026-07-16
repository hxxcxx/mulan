/*
 * tangent4 surface layout 的 PBR 实例化顶点着色器。
 */

#define MULAN_OBJECT_BATCH 1
#include "common.slang"

VS_OUTPUT_TANGENT main(VS_INPUT_TANGENT input, uint instanceId : SV_InstanceID) {
    VS_OUTPUT_TANGENT output;
    ObjectData object = Objects[instanceId];

    float4 worldPos = mul(object.World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(object.NormalMatrix, input.normal));
    output.texcoord = input.texcoord;

    float3 tangent = normalize(mul(object.NormalMatrix, input.tangent.xyz));
    output.tangent = float4(tangent, input.tangent.w);
    return output;
}
