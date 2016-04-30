#include "Lighting.h.hlsl"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 normal   : NORMAL0;
};

float4 main(PSInput i) : SV_Target
{
    float3 N = normalize(i.normal.xyz); // Renormalize after interpolation
    float3 radianceHDR = lighting(i.worldPos.xyz, N, i.uv.xy);
    float3 radianceLDR = toneMap(radianceHDR, tonemapMode, maxLuminance);
    // sRGB mapping done by hardware, so no manual gamma correction needed
    return float4(radianceLDR, 1);
}
