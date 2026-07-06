#include "common.hlsli"

float4 main(VS_OUTPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
    float3 n = normalize(input.normal);
    if (!isFrontFace) {
        n = -n;
    }

    float3 l = normalize(-LightDir);
    float ndotl = saturate(dot(n, l));

    float shade = 0.72 + 0.28 * ndotl;
    float3 color = saturate(BaseColor * shade);

    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    return float4(color, Alpha);
}
