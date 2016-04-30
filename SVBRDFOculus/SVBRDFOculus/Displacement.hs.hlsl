struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 uvTess   : TEXCOORD0;
    float4 normal   : NORMAL0;
};

// Output patch constant data.
struct HSPatchConstants
{
	float EdgeTessFactor[3]			: SV_TessFactor; // e.g. would be [4] for a quad domain
	float InsideTessFactor			: SV_InsideTessFactor; // e.g. would be Inside[2] for a quad domain
};

static const uint NumControlPoints = 3;

// Patch Constant Function
HSPatchConstants CalcHSPatchConstants(
	InputPatch<VSOutput, NumControlPoints> ip,
	uint PatchID : SV_PrimitiveID)
{
	HSPatchConstants pc;

    // Tessellation factors for the vertices
    float t0 = ip[0].uvTess.z;
    float t1 = ip[1].uvTess.z;
    float t2 = ip[2].uvTess.z;

    // Tessellation factors for the edges are the maximums
    // of their vertices.
    float e0 = max(t1, t2); // U=0 edge, so without v0
    float e1 = max(t0, t2); // V=0 edge, so without v1
    float e2 = max(t0, t1); // W=0 edge, so without v2

    // The inside tessellation factor is the minimum of the
    // vertex tessellation factors. Each vertex is guaranteed
    // to have at least as much tessellation factor as this
    // triangle required, so this tessellates at least as much as necessary.
    float i  = min(min(t0, t1), t2);

    pc.EdgeTessFactor[0] = e0;
    pc.EdgeTessFactor[1] = e1;
    pc.EdgeTessFactor[2] = e2;
    pc.InsideTessFactor  = i;

	return pc;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
VSOutput main(
	InputPatch<VSOutput, NumControlPoints> ip, 
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID)
{
	return ip[i];
}
