Texture2D<float4> text   : register(t0);
SamplerState textSampler : register(s0);

struct PSInput
{
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
};

float4 main(PSInput i) : SV_Target
{
    float4 color = text.Sample(textSampler, i.uv);
    float alpha = max(max(color.r, color.g), color.b);
    color *= i.color;
    return float4(color.rgb, alpha);
}
