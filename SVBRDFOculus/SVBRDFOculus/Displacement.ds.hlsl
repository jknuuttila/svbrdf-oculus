struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 uvTess   : TEXCOORD0;
    float4 normal   : NORMAL0;
};

struct DSOutput
{
    float4 worldPos : POSITION0;
    float4 uvTess   : TEXCOORD0;
    float4 normal   : NORMAL0;
    float4 svPos    : SV_Position;
};

struct HSPatchConstants
{
	float EdgeTessFactor[3]			: SV_TessFactor; // e.g. would be [4] for a quad domain
	float InsideTessFactor			: SV_InsideTessFactor; // e.g. would be Inside[2] for a quad domain
};

cbuffer DSConstants : register(b0)
{
    float4x4 viewProj;
    float scale;
    float displacementMagnitude;
};

Texture2D<float> heightmap : register(t0);
SamplerState heightSampler : register(s0);

static const uint NumControlPoints = 3;

float4 interpolate(float4 v0, float4 v1, float4 v2, float3 barycentric)
{
    return v0 * barycentric.x + v1 * barycentric.y + v2 * barycentric.z;
}
float3 interpolate(float3 v0, float3 v1, float3 v2, float3 barycentric)
{
    return v0 * barycentric.x + v1 * barycentric.y + v2 * barycentric.z;
}
float2 interpolate(float2 v0, float2 v1, float2 v2, float3 barycentric)
{
    return v0 * barycentric.x + v1 * barycentric.y + v2 * barycentric.z;
}

[domain("tri")]
DSOutput main(
	HSPatchConstants input,
	float3 barycentric : SV_DomainLocation,
	const OutputPatch<VSOutput, NumControlPoints> patch)
{
	DSOutput o;

    VSOutput v0 = patch[0];
    VSOutput v1 = patch[1];
    VSOutput v2 = patch[2];

    o.worldPos.xyz = interpolate(v0.worldPos.xyz, v1.worldPos.xyz, v2.worldPos.xyz, barycentric);
    o.worldPos.w   = 1;

    o.uvTess.xy    = interpolate(v0.uvTess.xy,    v1.uvTess.xy,    v2.uvTess.xy,    barycentric);
    o.uvTess.zw    = 0;

    o.normal.xyz   = interpolate(v0.normal.xyz,   v1.normal.xyz,   v2.normal.xyz,   barycentric);
    o.normal.w     = 0;

#if 1
    float3 N   = normalize(o.normal.xyz);
#else
    float3 v01 = v1.worldPos.xyz - v0.worldPos.xyz;
    float3 v02 = v2.worldPos.xyz - v0.worldPos.xyz;
    float3 N   = normalize(cross(v01, v02));
#endif

    // Scale has been applied in VS already, and the normal is in world space.
    float height       = heightmap.SampleLevel(heightSampler, o.uvTess.xy, 0);
    float displacement = height * displacementMagnitude;

	o.worldPos.xyz += displacement * N;
	o.svPos         = mul(o.worldPos, viewProj);

	return o;
}
