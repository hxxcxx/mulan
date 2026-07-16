/*
 * ViewCube 顶点着色器：颜色随顶点携带，使所有部件共享一次材质绑定。
 */

#include "common.slang"

VS_OUTPUT_COLOR main(VS_INPUT_COLOR input) {
    VS_OUTPUT_COLOR output;
    float4 worldPos = mul(World, float4(input.position, 1.0));
    output.position = mul(ViewProjection, worldPos);
    output.normal = normalize(mul(NormalMatrix, input.normal));
    output.color = input.color;
    return output;
}
