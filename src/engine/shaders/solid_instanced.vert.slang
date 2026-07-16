/*
 * Solid 实例化顶点着色器。
 * binding=1 为固定 128 项 ObjectBatch；CPU 保证 instanceId 不越界且 startInstance=0。
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
