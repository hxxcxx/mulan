/*
 * 传统材质光照。Lambert 与 Blinn-Phong 共用资源布局，仅后者计算高光项。
 */

#include "common.slang"
#include "shader_compat.slang"

#ifdef MULAN_OPENGL
[[vk::binding(3, 0)]]  Sampler2D BaseColorTex;
[[vk::binding(4, 0)]]  Sampler2D NormalTex;
[[vk::binding(5, 0)]]  Sampler2D SpecularTex;
[[vk::binding(6, 0)]]  Sampler2D EmissiveTex;
[[vk::binding(7, 0)]]  Sampler2D ShininessTex;
[[vk::binding(12, 0)]] Sampler2D AmbientTex;
[[vk::binding(13, 0)]] Sampler2D OpacityTex;
#else
[[vk::binding(3, 0)]]  Texture2D BaseColorTex : register(t3);
[[vk::binding(4, 0)]]  Texture2D NormalTex : register(t4);
[[vk::binding(5, 0)]]  Texture2D SpecularTex : register(t5);
[[vk::binding(6, 0)]]  Texture2D EmissiveTex : register(t6);
[[vk::binding(7, 0)]]  Texture2D ShininessTex : register(t7);
[[vk::binding(8, 0)]]  SamplerState MaterialSampler : register(s8);
[[vk::binding(12, 0)]] Texture2D AmbientTex : register(t12);
[[vk::binding(13, 0)]] Texture2D OpacityTex : register(t13);
#endif

static const uint TF_BASE_COLOR = 0x001u;
static const uint TF_NORMAL = 0x002u;
static const uint TF_EMISSIVE = 0x008u;
static const uint TF_AMBIENT = 0x020u;
static const uint TF_SPECULAR = 0x040u;
static const uint TF_SHININESS = 0x080u;
static const uint TF_OPACITY = 0x100u;
static const uint SHADING_BLINN_PHONG = 2u;
static const uint ALPHA_MASK = 1u;
static const uint ALPHA_BLEND = 2u;

float3 acesTonemap(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 perturbNormal(float3 normal, float3 worldPosition, float2 uv) {
    float3 tangentNormal = MULAN_SAMPLE_2D(NormalTex, MaterialSampler, uv).rgb * 2.0 - 1.0;
    float3 dp1 = ddx(worldPosition);
    float3 dp2 = ddy(worldPosition);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(determinant) < 1e-8)
        return normal;
    float3 tangent = normalize((duv2.y * dp1 - duv1.y * dp2) - normal * dot(normal, duv2.y * dp1 - duv1.y * dp2));
    float3 bitangent = normalize(cross(normal, tangent));
    return normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normal);
}

#ifdef MULAN_USE_VERTEX_TANGENT
float3 perturbNormal(float3 normal, float2 uv, float4 tangentFrame) {
    float3 tangentNormal = MULAN_SAMPLE_2D(NormalTex, MaterialSampler, uv).rgb * 2.0 - 1.0;
    float3 tangent = normalize(tangentFrame.xyz - normal * dot(normal, tangentFrame.xyz));
    float3 bitangent = normalize(cross(normal, tangent) * tangentFrame.w);
    return normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normal);
}
#endif

#ifdef MULAN_USE_VERTEX_TANGENT
float4 main(VS_OUTPUT_TANGENT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
#else
float4 main(VS_OUTPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
#endif
    float4 baseSample = ((TextureFlags & TF_BASE_COLOR) != 0u)
        ? MULAN_SAMPLE_2D(BaseColorTex, MaterialSampler, input.texcoord)
        : float4(1.0, 1.0, 1.0, 1.0);
    float alpha = Alpha * baseSample.a;
    if ((TextureFlags & TF_OPACITY) != 0u)
        alpha *= MULAN_SAMPLE_2D(OpacityTex, MaterialSampler, input.texcoord).r;
    if (AlphaMode == ALPHA_MASK && alpha < AlphaCutoff)
        discard;
    if (AlphaMode != ALPHA_BLEND)
        alpha = 1.0;

    float3 normal = normalize(input.normal);
    if (!isFrontFace)
        normal = -normal;
    if ((TextureFlags & TF_NORMAL) != 0u) {
#ifdef MULAN_USE_VERTEX_TANGENT
        normal = perturbNormal(normal, input.texcoord, input.tangent);
#else
        normal = perturbNormal(normal, input.worldPos, input.texcoord);
#endif
    }

    float3 diffuseColor = BaseColor * baseSample.rgb;
    // map_Ka 存在时使用显式环境反射贴图；否则以 diffuse 反射率调制 Ka。
    // 许多 MTL 导出器固定写入 Ka=1，若直接作为白色环境项会把贴图整体洗白。
    float3 ambientReflectance = AmbientFactor;
    if ((TextureFlags & TF_AMBIENT) != 0u)
        ambientReflectance *= MULAN_SAMPLE_2D(AmbientTex, MaterialSampler, input.texcoord).rgb;
    else
        ambientReflectance *= diffuseColor;
    float3 specularColor = Specular;
    if ((TextureFlags & TF_SPECULAR) != 0u)
        specularColor *= MULAN_SAMPLE_2D(SpecularTex, MaterialSampler, input.texcoord).rgb;
    float exponent = max(Shininess, 1.0);
    if ((TextureFlags & TF_SHININESS) != 0u)
        exponent *= MULAN_SAMPLE_2D(ShininessTex, MaterialSampler, input.texcoord).r;
    float3 emissive = Emissive;
    if ((TextureFlags & TF_EMISSIVE) != 0u)
        emissive *= MULAN_SAMPLE_2D(EmissiveTex, MaterialSampler, input.texcoord).rgb;

    float3 viewDirection = normalize(CameraPos - input.worldPos);
    float3 direct = float3(0.0, 0.0, 0.0);
    for (uint lightIndex = 0u; lightIndex < activeSceneLightCount(); ++lightIndex) {
        EvaluatedLight light = evaluateSceneLight(lightIndex, input.worldPos);
        float ndotl = max(dot(normal, light.directionToLight), 0.0);
        float3 contribution = diffuseColor * ndotl;
        if (ShadingModel == SHADING_BLINN_PHONG && ndotl > 0.0) {
            float3 halfVector = normalize(viewDirection + light.directionToLight);
            contribution += specularColor * pow(max(dot(normal, halfVector), 0.0), exponent);
        }
        direct += contribution * light.radiance;
    }

    float3 color = (ambientReflectance * AmbientColor + direct + emissive) * Exposure;
    color = acesTonemap(color);
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    return float4(color, alpha);
}
