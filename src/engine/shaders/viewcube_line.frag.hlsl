#include "common.hlsli"

float4 main(VS_OUTPUT_SIMPLE input) : SV_TARGET {
    return float4(saturate(BaseColor), Alpha);
}
