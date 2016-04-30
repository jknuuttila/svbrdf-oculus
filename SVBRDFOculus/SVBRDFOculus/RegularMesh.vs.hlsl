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

cbuffer VSConstants : register(b0)
{
    float4x4 viewProj;
    float scale;
    float displacementMagnitude;
};

VSOutput main(Vertex v)
{
    VSOutput o;
    float4 pos = float4(scale * v.pos, 1);
    float4 projPos = mul(pos, viewProj);
    // Vertex position and normal are already in world space.
    o.worldPos = pos;
    o.uvTess   = float4(v.uv, v.tess, 0);
    o.svPos    = projPos;
    o.normal   = float4(v.normal, 0);
	return o;
}
