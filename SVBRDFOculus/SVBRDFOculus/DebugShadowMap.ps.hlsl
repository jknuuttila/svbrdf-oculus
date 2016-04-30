#include "Lighting.h.hlsl"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 normal   : NORMAL0;
    float4 svPos    : SV_Position;
};

float4 main(PSInput i) : SV_Target
{
    row_major float4x4 viewProj = shadowViewProjs[5].M;
    float3 lPos = lights[0].positionWorld.xyz;
    float distanceFromLight = length(i.worldPos.xyz - lPos);

    static const float ShadowNearZ =   .1f;
    static const float ShadowFarZ  = 5.f;
    static const float ShadowRange = ShadowFarZ - ShadowNearZ;
    float viewD = (ShadowFarZ - distanceFromLight) / ShadowRange;
    float svD = i.svPos.z;

    float4 P  = float4(i.worldPos.xyz, 1);
    float4 P_  = mul(P, viewProj);
    P_.xyz    /= P_.w;
    float d    = P_.z;
    // PCF shadows
    float2 uv = (float2(P_.x, -P_.y) + 1) / 2; 
    return float4(svD, viewD, d, 1);
}
