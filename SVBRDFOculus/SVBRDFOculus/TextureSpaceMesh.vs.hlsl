struct Vertex
{
    float3 pos    : POSITION0;
    float3 normal : NORMAL0;
    float2 uv     : TEXCOORD0;
    float  tess   : COLOR0;
};

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 uvTess   : TEXCOORD0;
    float4 normal   : NORMAL0;
    float4 svPos    : SV_Position;
};

VSOutput main(Vertex v)
{
    VSOutput o;
    // FIXME: Do not assume input vertices are in world space
    o.worldPos = float4(v.pos, 1);
    o.uvTess   = float4(v.uv, v.tess, 0);

    o.svPos    = float4(
        lerp(-1, 1,     v.uv.x),
        lerp(-1, 1, 1 - v.uv.y),
        0,
        1);
    o.normal   = float4(v.normal, 0);

	return o;
}
