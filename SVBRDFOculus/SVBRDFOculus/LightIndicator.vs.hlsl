cbuffer Constants : register(b0)
{
    float4x4 viewProj;
    float4 position;
    float4 color;
    float size;
};

static const float rcpSqrt2 = 0.707;

// Tetrahedron
static const float4 vertices[4] =
{
    float4(-1,  0, -rcpSqrt2, 1),
    float4( 1,  0, -rcpSqrt2, 1),
    float4( 0, -1,  rcpSqrt2, 1),
    float4( 0,  1,  rcpSqrt2, 1),
};

// No thought given to winding, rendered with backface culling off
static const int indices[12] = {
    0, 1, 2,
    0, 1, 3,
    0, 2, 3,
    1, 2, 3,
};

struct VSOutput
{
    float4 color : COLOR0;
    float4 pos : SV_Position;
};

VSOutput main(uint id : SV_VertexID)
{
    VSOutput o;

    id = clamp(id, 0, 11);

    float4 v  = vertices[indices[id]] * size;
    v.xyz += position.xyz;
    v.w = 1;
    float4 v_ = mul(v, viewProj);

    o.pos   = v_;
    o.color = color;

	return o;
}