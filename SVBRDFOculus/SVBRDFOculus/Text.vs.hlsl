struct VSInput
{
    float2 pos   : POSITION0;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
    float4 pos : SV_Position;
};

VSOutput main(VSInput i)
{
    VSOutput o;
    o.pos   = float4(i.pos, 0, 1);
    o.uv    = i.uv;
    o.color = i.color;
	return o;
}
