
cbuffer Constants : register(b0)
{
    float4x4 viewProj;
};

// Static triangle on the XY plane.
static const float4 vertices[3] =
{
    float4(-1, -1, 0, 1),
    float4( 0,  1, 0, 1),
    float4( 1, -1, 0, 1),
};

static const float4 colors[3] =
{
    float4(1, 0, 0, 1),
    float4(0, 1, 0, 1),
    float4(0, 0, 1, 1),
};

struct VSOutput
{
    float4 color : COLOR0;
    float4 pos : SV_Position;
};

VSOutput main(uint id : SV_VertexID)
{
    VSOutput o;

    id = clamp(id, 0, 2);

    float4 v  = vertices[id];
    float4 v_ = mul(v, viewProj);

    o.pos   = v_;
    o.color = colors[id];

	return o;
}
