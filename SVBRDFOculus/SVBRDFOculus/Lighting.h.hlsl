#ifndef SVBRDF_LIGHTING_H_HLSL
#define SVBRDF_LIGHTING_H_HLSL

#define SHADOW_USE_COMPARISON_SAMPLER

#ifdef SHADOW_USE_COMPARISON_SAMPLER
#define ShadowSamplerType SamplerComparisonState
#else
#define ShadowSamplerType SamplerState
#endif

Texture2D<float4>  diffuseAlbedoMap  : register(t0);
Texture2D<float4>  specularAlbedoMap : register(t1);
Texture2D<float4>  specularShapeMap  : register(t2);
Texture2D<float4>  normalMap         : register(t3);
SamplerState materialSampler              : register(s0);
cbuffer PSConstants : register(b0)
{
    float4 ambientLight;
    float4 cameraPosition;
    float  alpha;
    uint   tonemapMode;
    float  maxLuminance;
    uint   normalMode;
    uint   useNormalMapping;
    uint   numLights;
};

struct Light
{
    float3 positionWorld;
    float  falloffMultiplier;
    float3 color;
    float  _padding;
};
StructuredBuffer<Light> lights : register(t4);

static const uint TonemapIdentity    = 0;
static const uint TonemapReinhard    = 1;
static const uint TonemapReinhardMod = 2;

static const uint NormalInterpolated  = 0;
static const uint NormalReconstructed = 1;
static const uint NormalConstant      = 2;

static const float3 ConstantUp = float3(0, 0, 1);

cbuffer ShadowConstants : register(b1)
{
    uint  shadowLights;
    uint  shadowPcfTaps;
    float shadowKernelWidth;
    float shadowMapResolution;
};

struct ShadowViewProj
{
    row_major float4x4 M;
};

Texture2DArray<float> shadowMaps                     : register(t5);
StructuredBuffer<ShadowViewProj> shadowViewProjs : register(t6);
ShadowSamplerType shadowSampler                      : register(s1);

// Enable this to display just the diffuse light intensity without albedo.
// #define DEBUG_ONLY_DIFFUSE_LIGHT

#define REMOVE_TEXTURE_IMPLICIT_CONSTANTS

#define BRDF_BRADY_ET_AL        0
#define BRDF_AITTALA            1

#define BRDF_MODE               BRDF_BRADY_ET_AL

struct Material
{
    float3 diffuseAlbedo;
    float3 specularAlbedo;
    float3 specularShape;
    float3 normal;
    float  alpha;
    float  F0;
};

struct Point
{
    float3 positionWorld;
    // World space tangent frame at the point such that:
    float3 tangentWorld;   // T = normal map positive X
    float3 bitangentWorld; // B = normal map positive Y
    float3 normalWorld;    // N = normal map positive Z
};

static const float Pi = 3.141592;

Material sampleSVBRDF(Texture2D<float4> diffuseAlbedoMap,
                    Texture2D<float4> specularAlbedoMap,
                    Texture2D<float4> specularShapeMap,
                    Texture2D<float4> normalMap,
                    float alpha, float F0,
                    SamplerState smp,
                    float2 uv)
{
    Material mat;
#if BRDF_MODE == BRDF_BRADY_ET_AL && defined(REMOVE_TEXTURE_IMPLICIT_CONSTANTS)
    // Assume, that the diffuse albedo in the texture is such that the
    // normalization factor of 1/Pi has already been implicitly incorporated.
    // Reverse it so we can use the unmodified BRDF formula.
    mat.diffuseAlbedo  = diffuseAlbedoMap.Sample(smp, uv).xyz * Pi;
    // Similarly, assume that:
    // - The specular albedo channel has an additional factor of 1/4 baked in.
    // - The value is scaled so that when V == L == H, the F term should
    //   become exactly 1 to match the optimizer. This means that the
    //   value should be divided by whatever we expect the actual F0 to be.
    mat.specularAlbedo = specularAlbedoMap.Sample(smp, uv).xyz * (4 / F0);
#else
    mat.diffuseAlbedo  = diffuseAlbedoMap.Sample(smp, uv).xyz;
    mat.specularAlbedo = specularAlbedoMap.Sample(smp, uv).xyz;
#endif
    mat.specularShape  = specularShapeMap.Sample(smp, uv).xyz;
    mat.normal         = normalize(normalMap.Sample(smp, uv).xyz);

    mat.alpha          = alpha;
    mat.F0             = F0;
    return mat;
}

uint shadowFaceSelect(float3 LToP)
{
    float3 absAxes = abs(LToP);
    if (absAxes.x > absAxes.y && absAxes.x > absAxes.z)
    {
        return (LToP.x >= 0) ? 0 : 1;
    }
    else if (absAxes.y > absAxes.x && absAxes.y > absAxes.z)
    {
        return (LToP.y >= 0) ? 2 : 3;
    }
    else
    {
        return (LToP.z >= 0) ? 4 : 5;
    }
}

static const float2 HaltonUnitSquare[] =
{
    float2(0.5, 0.5), // Always use the square center as the first tap
    float2(0.5, 0.3333333333333333),
    float2(0.25, 0.6666666666666666),
    float2(0.75, 0.1111111111111111),
    float2(0.125, 0.4444444444444444),
    float2(0.625, 0.7777777777777777),
    float2(0.375, 0.2222222222222222),
    float2(0.875, 0.5555555555555556),
    float2(0.0625, 0.8888888888888888),
    float2(0.5625, 0.037037037037037035),
    float2(0.3125, 0.37037037037037035),
    float2(0.8125, 0.7037037037037037),
    float2(0.1875, 0.14814814814814814),
    float2(0.6875, 0.48148148148148145),
    float2(0.4375, 0.8148148148148147),
    float2(0.9375, 0.25925925925925924),
    float2(0.03125, 0.5925925925925926),
    float2(0.53125, 0.9259259259259258),
    float2(0.28125, 0.07407407407407407),
    float2(0.78125, 0.4074074074074074),
    float2(0.15625, 0.7407407407407407),
    float2(0.65625, 0.18518518518518517),
    float2(0.40625, 0.5185185185185185),
    float2(0.90625, 0.8518518518518517),
    float2(0.09375, 0.2962962962962963),
    float2(0.59375, 0.6296296296296297),
    float2(0.34375, 0.9629629629629629),
    float2(0.84375, 0.012345679012345678),
    float2(0.21875, 0.345679012345679),
    float2(0.71875, 0.6790123456790123),
    float2(0.46875, 0.12345679012345678),
};

// fxc gives a false positive warning in debug mode because of the shadowMappingEnabled branch
// Warning	X4000	use of potentially uninitialized variable (evaluateShadowTerm)
#pragma warning(disable: 4000)
float evaluateShadowTerm(Point pt, Light light, uint lightIndex)
{
    // If shadow mapping is disabled or there are no shadow maps
    // for this light, no shadowing.
    if (lightIndex >= shadowLights)
        return 1;

    float4 P    = float4(pt.positionWorld, 1);
    float3 LToP = pt.positionWorld - light.positionWorld;

    uint face  = shadowFaceSelect(LToP);
    float slice = 6.0 * lightIndex + face;

    row_major float4x4 viewProj = shadowViewProjs[uint(slice)].M;
#if 0
    {
        uint uSlice = uint(slice);
        // fxc seems to assume column-major matrices in structured
        // buffers regardless of /Zpr, so load manually :(
        viewProj = float4x4(
            shadowViewProjs[uSlice    ],
            shadowViewProjs[uSlice + 1],
            shadowViewProjs[uSlice + 2],
            shadowViewProjs[uSlice + 3]);
    }
#endif

    float4 P_  = mul(P, viewProj);
    P_.xyz    /= P_.w;
    float d    = P_.z;
    // PCF shadows
    float2 uv = (float2(P_.x, -P_.y) + 1) / 2; 
    float2 xy = uv * shadowMapResolution;
    float shadow;

#   ifndef SHADOW_USE_COMPARISON_SAMPLER
        float3 pos = float3(uv, slice);
        float dShadow = shadowMaps.Sample(shadowSampler, pos);
        shadow = (d >= dShadow) ? 1 : 0;
#   else
        shadow = 0;
        float shadowMapPixelSize  = 1. / shadowMapResolution;
        for (uint i = 0; i < shadowPcfTaps; ++i)
        {
            float2 tapZeroOrigin = HaltonUnitSquare[i] - 0.5;
            float2 tapPixels     = tapZeroOrigin * shadowKernelWidth;
            float2 tapUVOffset   = tapPixels * shadowMapPixelSize;
            float3 pos           = float3(uv + tapUVOffset, slice);

            shadow += shadowMaps.SampleCmp(shadowSampler, pos, d);
        }
        shadow /= shadowPcfTaps;
#   endif

    return shadow;
}

float fresnelSchlick(float F0, float3 V, float3 H)
{
    float VdotH = max(0, dot(V, H));
    float F = F0 + (1 - F0) * pow(1 - VdotH, 5);
    return F;
}

// Compute the shortest rotation from one unit vector another.
struct RotationBetweenVectors
{
    float3 axis;
    float  cosTheta;
    float  sinTheta;
};
RotationBetweenVectors computeRotationFromTo(float3 fromNormalized, float3 toNormalized)
{
    static const float Epsilon = 1e-2;

    RotationBetweenVectors r;
    r.axis       = cross(fromNormalized, toNormalized);

    float len    = length(r.axis);
    float rcpLen = (len < Epsilon) ? 0 : (1 / len);

    r.axis    *= rcpLen;
    r.sinTheta = len;
    r.cosTheta = dot(fromNormalized, toNormalized);
    return r;
}

// https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula
float3 rotateWith(RotationBetweenVectors rotation, float3 v)
{
    float3 k = rotation.axis;
    float  c = rotation.cosTheta;
    float  s = rotation.sinTheta;

    float3 vRot = v * c
                + cross(k, v) * s
                + k * (dot(k, v) * (1 - c));

    return vRot;
}

struct Lighting
{
    // Point to be lighted
    Point pt;
    // V = world space point-to-view vector
    float3 V;
    // N = world space normal vector
    float3 N;
    // Rotation to normal oriented coordinates
    RotationBetweenVectors toNormalOriented;
    // Shape matrix
    float2x2 S;
    // Diffuse coefficient with proper normalization
    float3 diffuseCoeff;
};

Lighting computeLightingEnvironment(Material mat,
                                    Point pt,
                                    float3 cameraPositionWorld)
{
    Lighting l;
    l.pt = pt;
    l.V = normalize(cameraPositionWorld - pt.positionWorld);

    if (useNormalMapping)
    {
        // Perturb the geometric normal according to the normal map and the local
        // tangent frame.
        float3x3 TBN = float3x3(pt.tangentWorld.x, pt.bitangentWorld.x, pt.normalWorld.x,
                                pt.tangentWorld.y, pt.bitangentWorld.y, pt.normalWorld.y,
                                pt.tangentWorld.z, pt.bitangentWorld.z, pt.normalWorld.z);

        l.N = mul(TBN, mat.normal);
    }
    else
    {
        l.N = pt.normalWorld;
    }

    l.toNormalOriented = computeRotationFromTo(l.N, float3(0, 0, 1));

    float3 shape = mat.specularShape;
    l.S = float2x2(shape.x, shape.z,
                   shape.z, shape.y);

#if BRDF_MODE == BRDF_BRADY_ET_AL
    // Brady et al. (2014) Equation (9)
    l.diffuseCoeff = mat.diffuseAlbedo / Pi;
#elif BRDF_MODE == BRDF_AITTALA
    // Differences from Brady et al. result from incorporating
    // constant coefficients into the albedos, and from dropping
    // the Fresnel term from the optimizer entirely.
    l.diffuseCoeff = mat.diffuseAlbedo;
#endif

    return l;
}

// V and L must be in the same space that N is in.
float3 evaluateLight(Material mat,
                     Lighting l,       // Lighting environment at the point
                     Light light,      // The world space position, color and intensity of the light
                     float shadowTerm) // Shadow term for the light, multiplies the direct light but not the ambient
{
    float3 V = l.V;
    float3 N = l.N;

    // L = world space point-to-light vector
    float3 L = normalize(light.positionWorld - l.pt.positionWorld);

    // Check backfacing triangles with the geometric normal, not
    // the normal mapped one.
    bool isBackFacing = dot(l.pt.normalWorld, L) < 0;

    // H = halfway vector
    float3 H = normalize(V + L);
    float3 H_ = rotateWith(l.toNormalOriented, H);
    // h = tangent plane parametrized half-vector
    float2 h = H_.xy / H_.z;

    float2x2 S = l.S;
    // mul(v, M) treats v as a row vector, so it is equivalent to v^T * M
    float2 hT_S   = mul(h, S);
    float  hT_S_h = mul(hT_S, h);  

    // Microfacet distribution
    // Aittala et al. (2015) Equation (1) 
    float D = exp(-pow(abs(hT_S_h), mat.alpha / 2));

    float F = fresnelSchlick(mat.F0, V, H);

    // Evaluate BRDF 
    float3 diffuse = l.diffuseCoeff;
#if BRDF_MODE == BRDF_BRADY_ET_AL
    // Brady et al. (2014) Equation (9)
    float3 specular = mat.specularAlbedo * D * F / (4 * dot(L, H));
#elif BRDF_MODE == BRDF_AITTALA
    // Differences from Brady et al. result from incorporating
    // constant coefficients into the albedos, and from dropping
    // the Fresnel term from the optimizer entirely.
    float3 specular = mat.specularAlbedo * D * F / (mat.F0 * dot(L, H));
#else
#error Undefined BRDF mode!
#endif

#if defined(DEBUG_ONLY_DIFFUSE_LIGHT)
    diffuse = 1;
    specular = 0;
#endif

    float3 reflectance = (diffuse + specular);

    float distance    = length(L);
    float attenuation = 1 / (distance * distance) * light.falloffMultiplier;
    float cosineTerm  = max(0, dot(N, L));

    // Incident radiance of the light
    float3 L_i = light.color * shadowTerm;

    float3 radiance = isBackFacing
        ? 0
        : (L_i * attenuation * cosineTerm * reflectance);

    return radiance;
}

// Gamma encoding approximation
float3 gamma(float3 linearColor)
{
    return sqrt(linearColor);
}

float3 RGBToYCgCo(float3 rgb)
{
    static const float3x3 M = float3x3( .25, .5,  .25,
                                       -.25, .5, -.25,
                                        .5 ,  0, -.5);
    float3 yCgCo = mul(M, rgb);
    return yCgCo;
}

float3 YCgCoToRGB(float3 yCgCo)
{
    float3 rgb;
    float tmp = yCgCo.x - yCgCo.y;
    rgb.r     = tmp     + yCgCo.z;
    rgb.g     = yCgCo.x + yCgCo.y;
    rgb.b     = tmp     - yCgCo.z;
    return rgb;
}

float3 toneMap(float3 linearRGB, uint mode, float maxLuminance)
{
    // NOTE: There's likely a bug in here somewhere, because it seems that
    // sometimes the colors get distorted with very bright speculars.
    if (mode == TonemapIdentity)
    {
        return linearRGB;
    }
    else if (mode == TonemapReinhard)
    {
        float3 yCgCoHDR = RGBToYCgCo(linearRGB);
        float3 yCgCoLDR;
        yCgCoLDR.x  = yCgCoHDR.x / (1 + yCgCoHDR.x);
        yCgCoLDR.yz = yCgCoHDR.yz;
        float3 ldr = YCgCoToRGB(yCgCoLDR);
        return ldr;
    }
    else if (mode == TonemapReinhardMod)
    {
        float3 yCgCoHDR = RGBToYCgCo(linearRGB);
        float3 yCgCoLDR;
        yCgCoLDR.x  = (yCgCoHDR.x * (1 + yCgCoHDR.x / (maxLuminance * maxLuminance))) / (1 + yCgCoHDR.x);
        yCgCoLDR.yz = yCgCoHDR.yz;
        float3 ldr = YCgCoToRGB(yCgCoLDR);
        return ldr;
    }
    else
    {
        // Debug color to indicate tonemapping bug
        return float3(1, 0, 1);
    }
}

void reconstructTangentFrame(inout Point pt, float2 uv, float3 dp1, float3 dp2)
{
    float3 N = pt.normalWorld;

    // Optimized tangent frame reconstruction code originally from:
    // http://www.thetenthplanet.de/archives/1180
    // Modified a bit to suit this program

    float2 duv1 =  ddx_fine( uv );
    float2 duv2 = -ddy_fine( uv );
 
    // solve the linear system
    float3 dp2perp = cross( dp2, N );
    float3 dp1perp = cross( N, dp1 );
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
 
    T = normalize(T);
    B = normalize(B);

    pt.tangentWorld   = T;
    pt.bitangentWorld = B;
}

Point pointReconstructNormal(float3 positionWorld, float2 uv)
{
    Point pt;
    pt.positionWorld = positionWorld;

    float3 p = pt.positionWorld;

    // get edge vectors of the pixel triangle
    float3 dp1  =  ddx_fine( p );
    float3 dp2  = -ddy_fine( p );

	pt.normalWorld = normalize(cross(dp1, dp2));

    reconstructTangentFrame(pt, uv, dp1, dp2);

    return pt;
}

Point pointWithNormal(float3 positionWorld, float3 normalWorld, float2 uv)
{
    Point pt;
    pt.positionWorld = positionWorld;
	pt.normalWorld   = normalWorld;

    float3 p = pt.positionWorld;

    // get edge vectors of the pixel triangle
    float3 dp1  =  ddx_fine( p );
    float3 dp2  = -ddy_fine( p );

    reconstructTangentFrame(pt, uv, dp1, dp2);

    return pt;
}

float3 colorize(float x, float min, float max)
{
    if (x < 0)
    {
        float c = abs(x / min);
        return float3(c, 0, 0);
    }
    else
    {
        float c = abs(x / max);
        return float3(0, 0, c);
    }
}

float3 lighting(float3 positionWorld, float3 normalWorld, float2 uv)
{
    static const float dielectricF0 = 0.04;

    Material mat = sampleSVBRDF(diffuseAlbedoMap, specularAlbedoMap,
                                specularShapeMap, normalMap,
                                alpha, dielectricF0, materialSampler, uv);

    Point pt;
    switch (normalMode)
    {
    default:
    case NormalInterpolated:
        pt = pointWithNormal(positionWorld, normalWorld, uv);
        break;
    case NormalReconstructed:
        pt = pointReconstructNormal(positionWorld, uv);
        break;
    case NormalConstant:
        pt = pointWithNormal(positionWorld, ConstantUp, uv);
        break;
    }

    Lighting lighting = computeLightingEnvironment(mat, pt, cameraPosition.xyz);

    float3 radianceHDR = ambientLight.rgb * lighting.diffuseCoeff;

    for (uint i = 0; i < numLights; ++i)
    {
        Light light = lights[i];
        float shadowTerm = evaluateShadowTerm(pt, light, i);
        float3 lightRadianceHDR = evaluateLight(mat, lighting, light, shadowTerm);
        radianceHDR += lightRadianceHDR;
    }

    return radianceHDR;
}

#endif