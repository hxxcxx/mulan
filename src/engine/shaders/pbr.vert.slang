/*
 * PBR vertex shader.
 * Standard surface layout: position + normal + uv.
 */

#include "common.slang"

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    float4 worldPos = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal   = normalize(mul(NormalMatrix, input.normal));
    output.texcoord = input.texcoord;

    return output;
}
