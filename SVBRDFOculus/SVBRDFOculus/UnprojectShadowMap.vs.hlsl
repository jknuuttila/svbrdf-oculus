Texture2DArray<float> shadowMaps : register(t0);

cbuffer UnprojectConstants : register(b0)
{
    float4x4 viewProj;
    float4x4 shadowInvViewProj;
    uint slice;
    uint resolution;
};

float4 main(uint id : SV_VertexID) : SV_Position
{
    uint2 coords = uint2(
        id % resolution,
        id / resolution);

    float2 fCoords = coords;
    fCoords  += 0.5;
    fCoords  /= resolution;
    fCoords   = lerp(-1, 1, fCoords);
    fCoords.y = -fCoords.y;

    float z = shadowMaps[uint3(coords, slice)];

    if (z > 0)
    {
        float4 ndc   = float4(fCoords, z, 1);
        float4 world = mul(ndc, shadowInvViewProj);
        float4 projPos = mul(world, viewProj);
        return projPos;
    }
    else
    {
        return float4(1000, 1000, 1000, 1);
    }
}
