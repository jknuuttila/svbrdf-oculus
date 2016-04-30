#include "Lighting.h.hlsl"

Texture2D<float> heightmap : register(t7);
SamplerState heightSampler : register(s2);

cbuffer TextureSpacePSConstants : register(b2)
{
    float displacementMagnitude;
};

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 normal   : NORMAL0;
};

float4 main(PSInput i) : SV_Target
{
    // Because we know what the displacement should be at the pixel, we can easily
    // take it into account when lighting.

    float3 N = normalize(i.normal.xyz); // Renormalize after interpolation

    float height = heightmap.Sample(heightSampler, i.uv.xy);
    float displacement = height * displacementMagnitude;
    i.worldPos.xyz += N * displacement;

    float3 radianceHDR = lighting(i.worldPos.xyz, N, i.uv.xy);
    // Linear HDR color into the lighting texture
    return float4(radianceHDR, 1);
}
