/*
 * Edge 实例化顶点着色器；保持旧路径完全相同的深度偏移。
 */

#define MULAN_OBJECT_BATCH 1
#include "common.slang"

VS_OUTPUT_SIMPLE main(VS_INPUT input, uint instanceId : SV_InstanceID) {
    VS_OUTPUT_SIMPLE output;
    ObjectData object = Objects[instanceId];
    float4 worldPos = mul(object.World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.position.z -= 0.0005 * output.position.w;
    return output;
}
