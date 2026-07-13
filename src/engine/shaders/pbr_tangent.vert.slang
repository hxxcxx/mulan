/*
 * PBR vertex shader for surface layout with tangent4.
 */

#include "common.slang"

VS_OUTPUT_TANGENT main(VS_INPUT_TANGENT input) {
    VS_OUTPUT_TANGENT output;

    float4 worldPos = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal   = normalize(mul(NormalMatrix, input.normal));
    output.texcoord = input.texcoord;

    float3 tangent = normalize(mul(NormalMatrix, input.tangent.xyz));
    output.tangent = float4(tangent, input.tangent.w);

    return output;
}
