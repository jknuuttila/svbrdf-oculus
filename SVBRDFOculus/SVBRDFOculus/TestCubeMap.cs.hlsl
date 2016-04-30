TextureCube<float4> cubeMap : register(t0);
RWTexture2D<float4> output : register(u0);
SamplerState smp : register(s0);

static const uint RectSize = 128;
static const uint GroupSize = 16;

static const uint2 Rectangles[6] = {
    uint2(2, 1), // +X (0)
    uint2(0, 1), // -X (1)
    uint2(1, 0), // +Y (2)
    uint2(1, 2), // -Y (3)
    uint2(1, 1), // +Z (4)
    uint2(3, 1), // -Z (5)
};

float3 cubeDir(uint face, float2 coords)
{
    switch (face)
    {
    default:
    case 0:
        return float3( 1, coords.xy);
        break;
    case 1:
        return float3(-1, coords.xy);
        break;
    case 2:
        return float3(coords.x,  1, coords.y);
        break;
    case 3:
        return float3(coords.x, -1, coords.y);
        break;
    case 4:
        return float3(coords.xy,  1);
        break;
    case 5:
        return float3(coords.xy, -1);
        break;
    }
}

[numthreads(GroupSize, GroupSize, 1)]
void main(uint3 id : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint2 begin = Rectangles[gid.x] * RectSize;

    for (uint y = 0; y < RectSize; y += GroupSize)
    {
        for (uint x = 0; x < RectSize; x += GroupSize)
        {
            uint2 pos   = uint2(x, y) + id.xy;
            float2 fpos = float2(pos);
            float2 uv   = fpos / float(RectSize);
            float2 coords = float2(
                lerp(-1,  1, uv.x),
                lerp( 1, -1, uv.y));
            float3 dir = cubeDir(gid.x, coords);

            float4 color = cubeMap.SampleLevel(smp, dir, 0);
            output[begin + pos] = color;
        }
    }
}
