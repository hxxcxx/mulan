/*
 * 标准 surface layout 的 PBR 实例化顶点着色器。
 */

#define MULAN_OBJECT_BATCH 1
#include "common.slang"

VS_OUTPUT main(VS_INPUT input, uint instanceId : SV_InstanceID) {
    VS_OUTPUT output;
    ObjectData object = Objects[instanceId];

    float4 worldPos = mul(object.World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(object.NormalMatrix, input.normal));
    output.texcoord = input.texcoord;
    return output;
}
