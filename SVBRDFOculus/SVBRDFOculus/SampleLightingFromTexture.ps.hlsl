#include "Lighting.h.hlsl"

Texture2D<float4> lightingMap : register(t0);
SamplerState smp : register(s0);

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 normal   : NORMAL0;
};

float4 main(PSInput i) : SV_Target
{
    float3 radianceHDR = lightingMap.Sample(smp, i.uv.xy).rgb;
    float3 radianceLDR = toneMap(radianceHDR, tonemapMode, maxLuminance);
    // sRGB mapping done by hardware, so no manual gamma correction needed
    return float4(radianceLDR, 1);
}
