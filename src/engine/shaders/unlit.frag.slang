/*
 * 纹理无光照材质。颜色与透明度来自材质因子和贴图，不读取场景光源。
 */

#include "common.slang"
#include "shader_compat.slang"

#ifdef MULAN_OPENGL
[[vk::binding(3, 0)]]  Sampler2D BaseColorTex;
[[vk::binding(13, 0)]] Sampler2D OpacityTex;
#else
[[vk::binding(3, 0)]]  Texture2D BaseColorTex : register(t3);
[[vk::binding(8, 0)]]  SamplerState MaterialSampler : register(s8);
[[vk::binding(13, 0)]] Texture2D OpacityTex : register(t13);
#endif

static const uint TF_BASE_COLOR = 0x001u;
static const uint TF_OPACITY = 0x100u;
static const uint ALPHA_MASK = 1u;
static const uint ALPHA_BLEND = 2u;

float4 main(VS_OUTPUT input) : SV_TARGET {
    float4 sampled = ((TextureFlags & TF_BASE_COLOR) != 0u)
        ? MULAN_SAMPLE_2D(BaseColorTex, MaterialSampler, input.texcoord)
        : float4(1.0, 1.0, 1.0, 1.0);
    float alpha = Alpha * sampled.a;
    if ((TextureFlags & TF_OPACITY) != 0u)
        alpha *= MULAN_SAMPLE_2D(OpacityTex, MaterialSampler, input.texcoord).r;
    if (AlphaMode == ALPHA_MASK && alpha < AlphaCutoff)
        discard;
    if (AlphaMode != ALPHA_BLEND)
        alpha = 1.0;

    float3 color = max(BaseColor * sampled.rgb, float3(0.0, 0.0, 0.0));
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    return float4(color, alpha);
}
