#include "Utils.hpp"
#include "Graphics.hpp"

#include "RegularMesh.vs.h"
#include "Displacement.hs.h"
#include "Displacement.ds.h"
#include "RegularLighting.ps.h"
#include "TextureSpaceMesh.vs.h"
#include "TextureSpaceLighting.ps.h"
#include "SampleLightingFromTexture.ps.h"
#include "LightIndicator.vs.h"
#include "LightIndicator.ps.h"
#include "Text.vs.h"
#include "Text.ps.h"
#include "Wireframe.ps.h"
#include "UnprojectShadowMap.vs.h"

#include "TestCubeMap.cs.h"
#include "DebugShadowMap.ps.h"

#include <cstring>
#include <vector>
#include <array>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <type_traits>

using namespace DirectX;

static const unsigned DefaultWindowWidth  = 1600;
static const unsigned DefaultWindowHeight =  900;
static const float NearZ = .1f;
static const float FarZ  = 40.f;
static const float ShadowNearZ =   .1f;
static const float ShadowFarZ  = 50.f;
static const int ShadowResolution = 1024;
static const int ShadowDepthBias = -8;
static const float ShadowSSDepthBias = -1.f;
static const unsigned MaxLights = 1024;
static const unsigned ShadowPcfTaps = 4;
static const unsigned ShadowKernelWidth = 2;
static const float CtrlMultiplier = 5;
static const float LightPosExtent = FarZ;
static const float LightPosIncrement = 0.05f;
static const float LightMaxIntensity = 50.f;
static const float MaxTessellation = 64.f;

static const char CameraButtons[] = "WASD&%('";

//#define DEBUG_SHADOW_MAPS
//#define DEBUG_SHADOW_MATRICES
//#define DEBUG_SHADOW_TEXEL_UNPROJECT
#define SHADOW_USE_COMPARISON_SAMPLER

#define ENUM_VALUE_TOSTRING(Enum, Value) case Enum::Value: return #Value;

enum class LightingMode
{
    ForwardLighting,
    TextureSpaceLighting,
    Maximum = TextureSpaceLighting,
};

enum class TextureSpaceLightingPrecision
{
    Float11_11_10,
    Float16,
    Float32,
    Maximum = Float32,
};

enum class DisplacementMode
{
    NoDisplacement,
    GPUDisplacementMapping,
    CPUDisplacementMapping,
    Maximum = CPUDisplacementMapping,
};

enum class MeshMode
{
    SingleQuad,
    LoadedMesh,
    Maximum = LoadedMesh,
};

enum class NormalMode
{
    InterpolatedNormals,
    ReconstructedNormals,
    ConstantNormal,
    Maximum = ConstantNormal,
};

enum class ShadowMode
{
    NoShadows,
    ShadowMapping,
    Maximum = ShadowMapping,
};

enum class TonemapMode : uint
{
    Identity = 0,
    Reinhard = 1,
    ReinhardMod = 2,
    Maximum = ReinhardMod,
};

enum class AntialiasingMode
{
    NoAA,
    SSAA2x,
    SSAA4x,
    MSAA4x,
    Maximum = MSAA4x,
};

const char *enumToString(LightingMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(LightingMode, ForwardLighting)
        ENUM_VALUE_TOSTRING(LightingMode, TextureSpaceLighting)
    }
    return nullptr;
}
const char *enumToString(DisplacementMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(DisplacementMode, NoDisplacement)
        ENUM_VALUE_TOSTRING(DisplacementMode, CPUDisplacementMapping)
        ENUM_VALUE_TOSTRING(DisplacementMode, GPUDisplacementMapping)
    }
    return nullptr;
}
const char *enumToString(MeshMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(MeshMode, SingleQuad)
        ENUM_VALUE_TOSTRING(MeshMode, LoadedMesh)
    }
    return nullptr;
}
const char *enumToString(NormalMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(NormalMode, InterpolatedNormals)
        ENUM_VALUE_TOSTRING(NormalMode, ReconstructedNormals)
        ENUM_VALUE_TOSTRING(NormalMode, ConstantNormal)
    }
    return nullptr;
}
const char *enumToString(ShadowMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(ShadowMode, NoShadows)
        ENUM_VALUE_TOSTRING(ShadowMode, ShadowMapping)
    }
    return nullptr;
}
const char *enumToString(TonemapMode mode) {
    switch (mode) {
        ENUM_VALUE_TOSTRING(TonemapMode, Identity)
        ENUM_VALUE_TOSTRING(TonemapMode, Reinhard)
        ENUM_VALUE_TOSTRING(TonemapMode, ReinhardMod)
    }
    return nullptr;
}
const char *enumToString(AntialiasingMode mode) {
    switch (mode) {
    case AntialiasingMode::NoAA:   return "NoAA";
    case AntialiasingMode::MSAA4x: return "4x MSAA";
    case AntialiasingMode::SSAA2x: return "2x SSAA";
    case AntialiasingMode::SSAA4x: return "4x SSAA";
    }
    return nullptr;
}

float toDegrees(float rad)
{
    return rad / XM_PI * 180.f;
}

float toRadians(float deg)
{
    return deg / 180.f * XM_PI;
}

const char *QuickPresetFilenames[] = 
{
    "preset_01.svp",
    "preset_02.svp",
    "preset_03.svp",
    "preset_04.svp",
    "preset_05.svp",
    "preset_06.svp",
    "preset_07.svp",
    "preset_08.svp",
    "preset_09.svp",
    "preset_10.svp",
};

struct Light
{
    float3 positionWorld;     // world space position
    float  falloffMultiplier; // falloff is calculated as f/r^2, where f is this constant and
                              // r is distance from the lighted point. in particular: if f == 0, there is no falloff.
    float3 colorHDR;          // intensity of each RGB channel
    float _padding;
};

struct RenderingState
{
    // Name of the SVBRDF to use for the mesh. If empty, the first detected mesh is used instead.
    std::string svbrdfName;
    // If non-empty, the mesh to use for rendering. If empty, a quad is used instead.
    std::string meshName;
    // The VR head position offset will be multiplied by 10^(S/4), where S = this constant.
    int vrScale;
    // Density of displacement mapping. I.e. target amount of heightmap pixels per vertex.
    // If it's less than 1, displacement mapping is disabled.
    float displacementDensity;
    // Magnitude of displacement mapping. Each vertex will be perturbed by H * M, where
    // H is the filtered heightmap value and M = this constant.
    float displacementMagnitude;
    // Antialiasing mode to use.
    AntialiasingMode aaMode;
    // Tone mapping mode to use.
    TonemapMode tonemapMode;
    // Position of the camera.
    float3 cameraPosWorld;
    // Camera rotation around the global Z axis
    float cameraYawDegrees;
    // Camera rotation around its own X axis
    float cameraPitchDegrees;
    // Constant diffuse ambient intensity for RGB channels
    float3 ambientHDR;
    // The first N lights of all the lights will have shadows, where N = this constant.
    // If this is 0, shadows are disabled.
    unsigned shadowLights;
    // Shadow map resolution to use per face.
    unsigned shadowResolution;
    // Shadow map depth bias.
    int shadowDepthBias;
    // Shadow map slope scaled depth bias.
    float shadowSSDepthBias;
    // All lights in the scene.
    std::vector<Light> lights;

    RenderingState()
    {
        vrScale = 4;
        displacementDensity   = 16;
        displacementMagnitude = 0.01f;
        aaMode = AntialiasingMode::NoAA;
        tonemapMode = TonemapMode::Identity;

        cameraPosWorld[0]  = -5.5f;
        cameraPosWorld[1]  = -5.0f;
        cameraPosWorld[2]  =  2.5f;
        cameraYawDegrees   = toDegrees(-.85f);
        cameraPitchDegrees = toDegrees(1.15f);

        ambientHDR[0] = .05f;
        ambientHDR[1] = .05f;
        ambientHDR[2] = .05f;

        shadowLights      = 1;
        shadowResolution  = ShadowResolution;
        shadowDepthBias   = ShadowDepthBias;
        shadowSSDepthBias = ShadowSSDepthBias;

        lights.emplace_back();
        lights[0].positionWorld[0] = 3.f;
        lights[0].positionWorld[1] = 3.f;
        lights[0].positionWorld[2] = 3.f;
        lights[0].falloffMultiplier = 1.f;
        lights[0].colorHDR[0] = 1.f;
        lights[0].colorHDR[1] = 1.f;
        lights[0].colorHDR[2] = 1.f;
    }

    void save(FILE *f) const
    {
        fprintf_s(f, "# Comments start with '#'\n");
        fprintf_s(f, "svbrdf %s    # Material name from the data directory\n", svbrdfName.c_str());

        if (!meshName.empty())
            fprintf_s(f, "mesh %s    # Mesh name from the data directory\n", meshName.c_str());
        else
            fprintf_s(f, "# mesh <mesh-name>    # Mesh name from the data directory\n");

        fprintf_s(f, "\n");

        fprintf_s(f, "vr_scale     %d    # Head position multiplied by 10^(%d / 4)\n", vrScale, vrScale);

        fprintf_s(f, "\n");

        fprintf_s(f, "displacement_density   %f    # About %f heightmap pixels per vertex. 0 = disabled.\n", displacementDensity, displacementDensity);
        fprintf_s(f, "displacement_magnitude %f\n", displacementMagnitude);

        fprintf_s(f, "\n");

        switch (aaMode)
        {
        case AntialiasingMode::NoAA:
        default:
            fprintf_s(f, "aa 0    # No antialiasing\n");
            break;
        case AntialiasingMode::SSAA2x:
            fprintf_s(f, "aa 2    # SSAA 2x\n");
            break;
        case AntialiasingMode::SSAA4x:
            fprintf_s(f, "aa 4    # SSAA 4x\n");
            break;
        case AntialiasingMode::MSAA4x:
            fprintf_s(f, "aa m    # MSAA 4x\n");
            break;
        }

        switch (tonemapMode)
        {
        case TonemapMode::Identity:
        default:
            fprintf_s(f, "tonemap 0    # Identity tone mapping\n");
            break;
        case TonemapMode::Reinhard:
            fprintf_s(f, "tonemap r    # Reinhard tone mapping\n");
            break;
        case TonemapMode::ReinhardMod:
            fprintf_s(f, "tonemap m    # Reinhard modified tone mapping with fixed exposure\n");
            break;
        }

        fprintf_s(f, "\n");

        fprintf_s(f, "camera_position      %f %f %f\n", cameraPosWorld[0],       cameraPosWorld[1],      cameraPosWorld[2]);
        fprintf_s(f, "camera_yaw_degrees   %f\n", cameraYawDegrees);
        fprintf_s(f, "camera_pitch_degrees %f\n", cameraPitchDegrees);

        fprintf_s(f, "\n");

        fprintf_s(f, "ambient %f %f %f    # Constant diffuse HDR ambient\n", ambientHDR[0], ambientHDR[1], ambientHDR[2]);

        fprintf_s(f, "\n");

        fprintf_s(f, "shadow_lights                  %u    # First %u lights will have shadows\n", shadowLights, shadowLights);
        fprintf_s(f, "shadow_resolution              %u    # Shadow map resolution\n", shadowResolution);
        fprintf_s(f, "shadow_depth_bias              %d\n", shadowDepthBias);
        fprintf_s(f, "shadow_slope_scaled_depth_bias %f\n", shadowSSDepthBias);

        for (auto &l : lights)
        {
            fprintf_s(f, "\n");
            fprintf_s(f, "light\n");
            fprintf_s(f, "light_position %f %f %f\n", l.positionWorld[0], l.positionWorld[1], l.positionWorld[2]);
            fprintf_s(f, "light_falloff  %f          # Multiplier for falloff\n", l.falloffMultiplier);
            fprintf_s(f, "light_color    %f %f %f    # HDR color\n", l.colorHDR[0], l.colorHDR[1], l.colorHDR[2]);
        }
    }

    bool save(const std::string &path) const
    {
        if (path.empty())
            return false;

        FILE *f = nullptr;
        if (fopen_s(&f, path.c_str(), "wt") == 0)
        {
            save(f);
            fclose(f);
            log("Saved preset \"%s\"\n", path.c_str());
            return true;
        }
        else
        {
            log("Failed to save preset \"%s\"\n", path.c_str());
            return false;
        }
    }

    bool saveAs() const
    {
        return save(fileSaveDialog("SVBRDF renderer preset", "*.svp"));
    }

    void load(FILE *f)
    {
        *this = RenderingState();
        lights.clear();

        char line[1024] = {0};

        while (fgets(line, sizeof(line), f))
        {
            char light[6];
            char path[512];
            char c;
            unsigned u;
            int i;
            float3 f3;

            if (sscanf_s(line, "svbrdf %511s", path, static_cast<int>(sizeof(path))) == 1)
            {
                svbrdfName = path;
            }
            else if (sscanf_s(line, "mesh %511s", path, static_cast<int>(sizeof(path))) == 1)
            {
                meshName = path;
            }
            else if (sscanf_s(line, "vr_scale %d", &i) == 1)
            {
                vrScale = i;
            }
            else if (sscanf_s(line, "displacement_density %f", &f3[0]) == 1)
            {
                displacementDensity = f3[0];
            }
            else if (sscanf_s(line, "displacement_magnitude %f", &f3[0]) == 1)
            {
                displacementMagnitude = f3[0];
            }
            else if (sscanf_s(line, "aa %c", &c, 1) == 1)
            {
                switch (c)
                {
                case '0':
                default:
                    aaMode = AntialiasingMode::NoAA; break;
                case '2':
                    aaMode = AntialiasingMode::SSAA2x; break;
                case '4':
                    aaMode = AntialiasingMode::SSAA4x; break;
                case 'm':
                case 'M':
                    aaMode = AntialiasingMode::MSAA4x; break;
                }
            }
            else if (sscanf_s(line, "tonemap %c", &c, 1) == 1)
            {
                switch (c)
                {
                case '0':
                default:
                    tonemapMode = TonemapMode::Identity; break;
                case 'r':
                case 'R':
                    tonemapMode = TonemapMode::Reinhard; break;
                case 'm':
                case 'M':
                    tonemapMode = TonemapMode::ReinhardMod; break;
                }
            }
            else if (sscanf_s(line, "camera_position %f %f %f", &f3[0], &f3[1], &f3[2]) == 3)
            {
                cameraPosWorld = f3;
            }
            else if (sscanf_s(line, "camera_yaw_degrees %f", &f3[0]) == 1)
            {
                cameraYawDegrees = f3[0];
            }
            else if (sscanf_s(line, "camera_pitch_degrees %f", &f3[0]) == 1)
            {
                cameraPitchDegrees = f3[0];
            }
            else if (sscanf_s(line, "ambient %f %f %f", &f3[0], &f3[1], &f3[2]) == 3)
            {
                ambientHDR = f3;
            }
            else if (sscanf_s(line, "shadow_lights %u", &u) == 1)
            {
                shadowLights = u;
            }
            else if (sscanf_s(line, "shadow_resolution %u", &u) == 1)
            {
                shadowResolution = u;
            }
            else if (sscanf_s(line, "shadow_depth_bias %d", &i) == 1)
            {
                shadowDepthBias = i;
            }
            else if (sscanf_s(line, "shadow_slope_scaled_depth_bias %f", &f3[0]) == 1)
            {
                shadowSSDepthBias = f3[0];
            }
            else if (sscanf_s(line, "light_position %f %f %f", &f3[0], &f3[1], &f3[2]) == 3)
            {
                if (!lights.empty())
                    lights.back().positionWorld = f3;
            }
            else if (sscanf_s(line, "light_falloff %f", &f3[0]) == 1)
            {
                if (!lights.empty())
                    lights.back().falloffMultiplier = f3[0];
            }
            else if (sscanf_s(line, "light_color %f %f %f", &f3[0], &f3[1], &f3[2]) == 3)
            {
                if (!lights.empty())
                    lights.back().colorHDR = f3;
            }
            else if (sscanf_s(line, "%5s", light, static_cast<int>(sizeof(light))) == 1 && strcmp(light, "light") == 0)
            {
                lights.emplace_back();
            }
        }

        if (!(displacementDensity >= 1.f))
        {
            displacementDensity = 0;
        }
    }

    bool load(const std::string &path)
    {
        if (path.empty())
            return false;

        FILE *f = nullptr;
        if (fopen_s(&f, path.c_str(), "rt") == 0)
        {
            load(f);
            fclose(f);
            return true;
        }
        else
        {
            log("Failed to load \"%s\"\n", path.c_str());
            return false;
        }
    }

    bool load()
    {
        return load(fileOpenDialog("SVBRDF renderer preset", "*.svp"));
    }
};

struct SVBRDF
{
    std::string name;
    std::string path;
    unsigned width;
    unsigned height;
    Resource diffuseAlbedo;
    Resource specularAlbedo;
    Resource specularShape;
    Resource normals;
    Resource heightMap;
    FloatPixelBuffer heightMapCPU;
    float alpha;

    bool valid() const
    {
        return !!diffuseAlbedo.texture;
    }
};

SVBRDF loadSVBRDF(std::string rootPath, std::string name)
{
    SVBRDF svbrdf;
    svbrdf.name = name;

    log("Loading SVBRDF \"%s\"...\n", svbrdf.name.c_str());

    Timer t;

    std::string path           = rootPath + "/" + name;
    std::string mapPath        = path + "/out/reverse/";
    std::string diffusePath    = mapPath + "map_diff.pfm";
    std::string specularPath   = mapPath + "map_spec.pfm";
    std::string specShapePath  = mapPath + "map_spec_shape.pfm";
    std::string normalPath     = mapPath + "map_normal.pfm";
    std::string paramsPath     = mapPath + "map_params.dat";

    size_t bytes = 0;

    svbrdf.path           = path;
    svbrdf.diffuseAlbedo  = loadImage(diffusePath.c_str(), &bytes);
    svbrdf.specularAlbedo = loadImage(specularPath.c_str(), &bytes);
    svbrdf.specularShape  = loadImage(specShapePath.c_str(), &bytes);
    svbrdf.normals        = loadImage(normalPath.c_str(), &bytes);

    RESOURCE_DEBUG_NAME(svbrdf.diffuseAlbedo);
    RESOURCE_DEBUG_NAME(svbrdf.specularAlbedo);
    RESOURCE_DEBUG_NAME(svbrdf.specularShape);
    RESOURCE_DEBUG_NAME(svbrdf.normals);

    auto heightMapFiles = searchFiles(rootPath, "normals_" + name + ".pfm");
    std::string heightMapPath;
    if (!heightMapFiles.empty())
        heightMapPath = heightMapFiles.at(0);

    if (heightMapPath.empty())
    {
        log("Could not find heightmap for \"%s\". Displacement mapping disabled.\n", name.c_str());
    }
    else
    {
        svbrdf.heightMap = loadPFMImage(heightMapPath.c_str(), &svbrdf.heightMapCPU);
        bytes += svbrdf.heightMapCPU.bytes();
        RESOURCE_DEBUG_NAME(svbrdf.heightMap);
    }

    {
        FILE *f = nullptr;
        fopen_s(&f, paramsPath.c_str(), "r");
        int got = fscanf_s(f, "%f", &svbrdf.alpha);
        check(got == 1, "Failed to read BRDF alpha parameter");
        fclose(f);
    }

    svbrdf.width  = static_cast<unsigned>(svbrdf.diffuseAlbedo.textureDescriptor().Width);
    svbrdf.height = static_cast<unsigned>(svbrdf.diffuseAlbedo.textureDescriptor().Height);

    double MB   = static_cast<double>(bytes) / (1024 * 1024);
    double secs = t.seconds();

    log("Loaded %u x %u (%.2f MB) in %.2f s (%.2f MB/s)\n",
        svbrdf.width, svbrdf.height, MB, secs, MB / secs);

    return svbrdf;
}

float adjustIncrement(float incrementOrMultiplier)
{
    if (keyHeld(VK_CONTROL))
        return incrementOrMultiplier * CtrlMultiplier;
    else
        return incrementOrMultiplier;
}

bool updateValueClamp(char increaseKey, char decreaseKey, float &value, float increment, float min, float max = 1e9f)
{
    bool changed = false;

    if (keyHeld(increaseKey))
    {
        value += adjustIncrement(increment);
        changed = true;
    }

    if (keyHeld(decreaseKey))
    {
        value -= adjustIncrement(increment);
        changed = true;
    }

    if (value < min) value = min;
    if (value >= max) value = max;

    return changed;
}

bool updateValueMultiply(char increaseKey, char decreaseKey, float &value, float multiplier, float min, float max = 1e9f, bool onPressed = false)
{
    bool changed = false;

    bool increase, decrease;
    if (onPressed)
    {
        increase = keyPressed(increaseKey);
        decrease = keyPressed(decreaseKey);
    }
    else
    {
        increase = keyHeld(increaseKey);
        decrease = keyHeld(decreaseKey);
    }

    if (increase)
    {
        value *= adjustIncrement(multiplier);
        changed = true;
    }

    if (decrease)
    {
        value /= adjustIncrement(multiplier);
        changed = true;
    }

    if (value < min) value = min;
    if (value >= max) value = max;

    return changed;
}

bool updateValueWrap(char increaseKey, char decreaseKey, float &value, float increment, float min, float max)
{
    bool changed = false;
    float range = max - min;

    if (keyHeld(increaseKey))
    {
        value += adjustIncrement(increment);
        changed = true;
    }

    if (keyHeld(decreaseKey))
    {
        value -= adjustIncrement(increment);
        changed = true;
    }

    while (value < min) value += range;
    while (value >= max) value -= range;

    return changed;
}

bool updateValueClamp(char increaseKey, char decreaseKey, int &value, int increment, int min, int max)
{
    bool changed = false;
    int range = max - min;

    if (keyPressed(increaseKey)) 
    {
        value += increment;
        changed = true;
    }

    if (keyPressed(decreaseKey))
    {
        value -= increment;
        changed = true;
    }

    if (min >= max)
    {
        value = min;
    }
    else
    {
        if (value < min) value = min;
        if (value > max) value = max;
    }

    return changed;
}

bool updateValueMax(char increaseKey, char decreaseKey, unsigned &value, unsigned max)
{
    bool changed = false;

    if (keyPressed(increaseKey)) 
    {
        if (value < max)
        {
            ++value;
            changed = true;
        }
    }

    if (keyPressed(decreaseKey))
    {
        if (value > 0)
        {
            --value;
            changed = true;
        }
    }

    return changed;
}

bool updateValueWrap(char increaseKey, char decreaseKey, int &value, int increment, int min, int max)
{
    bool changed = false;
    int range = max - min;

    if (keyPressed(increaseKey)) 
    {
        value += increment;
        changed = true;
    }

    if (keyPressed(decreaseKey))
    {
        value -= increment;
        changed = true;
    }

    if (min >= max)
    {
        value = min;
    }
    else
    {
        while (value < min) value += range;
        while (value >= max) value -= range;
    }

    return changed;
}

template <typename Enum>
bool updateEnum(char increaseKey, char decreaseKey, Enum &value, Enum min, Enum max)
{
    static_assert(std::is_enum<Enum>::value, "Only enums supported.");
    typedef typename std::underlying_type<Enum>::type T;
    bool changed = false;

    T v = static_cast<T>(value);
    T minT = static_cast<T>(min);
    T maxT = static_cast<T>(max);

    if (keyPressed(increaseKey)) 
    {
        if (v == maxT)
            v = minT;
        else
            ++v;

        changed = true;
    }

    if (keyPressed(decreaseKey))
    {
        if (v == minT)
            v = maxT;
        else
            --v;
        changed = true;
    }

    if (changed)
    {
        value = static_cast<Enum>(v);
        return true;
    }
    else
    {
        return false;
    }
}

bool toggleValue(const char *name, char toggleKey, bool &value)
{
    if (keyPressed(toggleKey))
    {
        value = !value;
        log("%s: %s\n", name, value ? "true" : "false");
        return true;
    }
    else
    {
        return false;
    }
}

template <typename Enum>
bool toggleValue(const char *name, char toggleKey, Enum &value)
{
    static_assert(std::is_enum<Enum>::value, "Only enums supported.");

    typedef typename std::underlying_type<Enum>::type T;

    static const Enum max = Enum::Maximum;
    static const T maxT   = static_cast<T>(max);

    if (keyPressed(toggleKey))
    {
        T v = static_cast<T>(value);

        if (v == maxT)
            v = 0;
        else
            v = v + 1;

        value = static_cast<Enum>(v);

        log("%s: %lld\n", name, static_cast<long long>(v));

        return true;
    }
    else
    {
        return false;
    }
}

XMVECTOR toVec(float3 f3, float w = 0)
{
    auto v = XMVectorSet(f3[0], f3[1], f3[2], w);
    return v;
}

float3 toF3(XMVECTOR v)
{
    return float3 { XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v) };
}

class AzimuthAltitude
{
    const char *buttonsWASDQE;
    float azimuth;
    float altitude;
    float distance;
public:
    AzimuthAltitude(const char *buttonsWASDQE, float azimuth, float altitude, float distance)
        : buttonsWASDQE(buttonsWASDQE)
        , azimuth(azimuth)
        , altitude(altitude)
        , distance(distance)
    {}

    void update()
    {
        static const float circle    = 2 * XM_PI;
        static const float angleIncrement = XM_PI / 180;
        static const float distIncrement = 0.05f;
        static const float minDist   = distIncrement;

        char up    = buttonsWASDQE[0];
        char left  = buttonsWASDQE[1];
        char down  = buttonsWASDQE[2];
        char right = buttonsWASDQE[3];
        char backward = buttonsWASDQE[4];
        char forward  = buttonsWASDQE[5];

        updateValueWrap(  up,  down, altitude, angleIncrement, 0, circle / 4);
        updateValueWrap(left, right,  azimuth, angleIncrement, 0, circle);
        updateValueClamp(forward, backward, distance, distIncrement, distIncrement);
    }

    XMVECTOR position() const
    {
        float x = distance * cos(altitude) * cos(azimuth);
        float y = distance * cos(altitude) * sin(azimuth);
        float z = distance * sin(altitude);
        return XMVectorSet(x, y, z, 1);
    }
};

class FPSCamera
{
    const char *buttonsWASDArrowsULDR;
    XMVECTOR pos;
    float rotLR;
    float rotUD;

public:
    FPSCamera(const char *buttonsWASDArrowsULDR, XMVECTOR initialPos, float initialLR, float initialUD)
        : buttonsWASDArrowsULDR(buttonsWASDArrowsULDR)
        , pos(initialPos)
        , rotLR(initialLR)
        , rotUD(initialUD)
    {
    }

    void update()
    {
        static const float turn = .02f;
        float move = adjustIncrement(0.05f);

        char forward     = buttonsWASDArrowsULDR[0];
        char strafeLeft  = buttonsWASDArrowsULDR[1];
        char backward    = buttonsWASDArrowsULDR[2];
        char strafeRight = buttonsWASDArrowsULDR[3];
        char turnUp      = buttonsWASDArrowsULDR[4];
        char turnLeft    = buttonsWASDArrowsULDR[5];
        char turnDown    = buttonsWASDArrowsULDR[6];
        char turnRight   = buttonsWASDArrowsULDR[7];

        XMVECTOR movementView = XMVectorZero();

        if (keyHeld(forward))
            movementView = XMVectorAdd(movementView, XMVectorSet(0, 0, -move, 0));
        else if (keyHeld(backward))
            movementView = XMVectorAdd(movementView, XMVectorSet(0, 0,  move, 0));

        if (keyHeld(strafeLeft))
            movementView = XMVectorAdd(movementView, XMVectorSet(-move, 0, 0, 0));
        else if (keyHeld(strafeRight))
            movementView = XMVectorAdd(movementView, XMVectorSet( move, 0, 0, 0));

        if (keyHeld(turnLeft))
            rotLR += turn;
        else if (keyHeld(turnRight))
            rotLR -= turn;

        if (keyHeld(turnUp))
            rotUD += turn;
        else if (keyHeld(turnDown))
            rotUD -= turn;

        rotLR = std::fmod(rotLR, 2 * XM_PI);
        rotUD = std::fmod(rotUD, 2 * XM_PI);

        XMVECTOR rot = rotation();
        pos = XMVectorAdd(pos, XMVector3Rotate(movementView, rot));
    }

    XMVECTOR position() const
    {
        return pos;
    }

    XMVECTOR rotation() const
    {
        auto LR     = XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), rotLR);
        auto UDAxis = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), LR);
        auto UD     = XMQuaternionRotationAxis(UDAxis, rotUD);
        return XMQuaternionMultiply(LR, UD);
    }

    float yaw() const
    {
        return rotLR;
    }

    float pitch() const
    {
        return rotUD;
    }
};

XMVECTOR quaternionFromTo(XMVECTOR from, XMVECTOR to)
{
    XMVECTOR axis = XMVector3Cross(from, to);
    float fLen2 = XMVectorGetX(XMVector3LengthSq(from));
    float tLen2 = XMVectorGetX(XMVector3LengthSq(to));
    float dot   = XMVectorGetX(XMVector3Dot(from, to));
    float w     = sqrt(fLen2 * tLen2) + dot;
    XMVECTOR q  = axis;
    q = XMVectorSetW(q, w);
    q = XMQuaternionNormalize(q);
    return q;
}

XMVECTOR quaternionLookAtRH(XMVECTOR pos, XMVECTOR target)
{
    XMVECTOR forwardWorld = XMVector3Normalize(XMVectorSubtract(target, pos));
    XMVECTOR forwardRH    = XMVectorSet(0, 0, -1, 0);
    XMVECTOR rot          = quaternionFromTo(forwardWorld, forwardRH);
    return rot;
}

std::vector<XMVECTOR> debugMatrix(XMMATRIX mat, std::vector<XMVECTOR> vs)
{
    log("----\n");
    for (auto &v : vs) log("{%f,%f,%f,%f}\n",
                           XMVectorGetByIndex(v, 0),
                           XMVectorGetByIndex(v, 1),
                           XMVectorGetByIndex(v, 2),
                           XMVectorGetByIndex(v, 3));

    for (auto &v : vs) v = XMVector4Transform(v, mat);

    for (auto &v : vs) log("{%f,%f,%f,%f}\n",
                           XMVectorGetByIndex(v, 0),
                           XMVectorGetByIndex(v, 1),
                           XMVectorGetByIndex(v, 2),
                           XMVectorGetByIndex(v, 3));
    return vs;
}

CD3D11_RASTERIZER_DESC rasterizerDesc(bool rightHanded, int depthBias = 0, float ssDepthBias = 0)
{
    return CD3D11_RASTERIZER_DESC(
        D3D11_FILL_SOLID,
        D3D11_CULL_BACK,
        rightHanded,
        depthBias,
        0,
        ssDepthBias,
        false, false, false, false);
}

template <typename Vertex, typename VS>
static CComPtr<ID3D11InputLayout> inputLayoutFor(const VS &vs)
{
    auto inputElements = Vertex::inputLayoutDesc();

    CComPtr<ID3D11InputLayout> layout;

    checkHR(device->CreateInputLayout(inputElements.data(),
                                      static_cast<UINT>(inputElements.size()),
                                      vs, sizeBytes(vs),
                                      &layout));

    return layout;
}

class LightIndicator
{
    GraphicsPipeline lightIndicator;

    struct Constants
    {
        XMMATRIX viewProj;
        XMVECTOR position;
        XMVECTOR color;
        float size;
    };
public:
    LightIndicator()
    {
        lightIndicator = GraphicsPipeline(
            lightindicator_vs,
            lightindicator_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &CD3D11_RASTERIZER_DESC(D3D11_FILL_SOLID, D3D11_CULL_NONE, false, 0, 0, 0, false, false, false, false));
    }

    void render(ConstantBuffers &cb, float size, XMVECTOR pos, XMMATRIX viewProj, float r, float g, float b)
    {
        Constants constants;
        constants.viewProj = viewProj;
        constants.position = pos;
        constants.color    = XMVectorSet(r, g, b, 1);
        constants.size     = size;

        lightIndicator.bind();
        auto vsCB = cb.write(constants);
        context->VSSetConstantBuffers(0, 1, bind(vsCB));
        context->Draw(12, 0);
    }
};

class TextManager
{
public:
    static const unsigned TextCacheW = 256;
    static const unsigned TextCacheH = 2048;

    static const unsigned MaxTextLen = 256;
    static const unsigned MaxTexts   = 256;
    static const unsigned RowMargin  = 2;
    static const unsigned ColMargin  = 16;

    typedef char TextBuffer[MaxTextLen];
    typedef std::function<void(TextBuffer &)> TextUpdateCallback;
private:

    struct Text
    {
        std::string text;
        TextUpdateCallback updateText;
        float2 uvUpperLeft;
        float2 uvLowerRight;
        unsigned width;
        unsigned height;
        int cacheY;
        float3 color;

        Text()
        {
            uvUpperLeft   = { 0, 0 };
            uvLowerRight  = { 0, 0 };
            width = 0;
            height = 0;
            cacheY = -1;
            color  = {0, 0, 0};
        }

        bool update()
        {
            char textBuffer[MaxTextLen] = {0};
            if (updateText)
            {
                updateText(textBuffer);
                if (text != textBuffer)
                {
                    text = textBuffer;
                    return true;
                }
            }

            return false;
        }

        bool isCached() const
        {
            return cacheY >= 0;
        }
    };

private:

    FontRasterizer fontRasterizer;
    Resource textCache;
    Resource vertexBuffer;
    Resource indexBuffer;
    GraphicsPipeline textPipeline;
    CComPtr<ID3D11SamplerState> textSampler;
    std::vector<std::vector<Text>> columns;
    unsigned nextFreeCacheY;

    struct Vertex
    {
        float2 pos;
        float2 uv;
        float4 color;

        static std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc()
        {
            return {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,                            0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
        }
    };

public:
    TextManager(unsigned columns = 1)
        : fontRasterizer({"Consolas", "Courier New"}, 12)
        , columns(columns)
    {
        auto textCacheDesc = texture2DDesc(TextCacheW, TextCacheH, DXGI_FORMAT_B8G8R8A8_UNORM);
        textCacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        textCache = Resource(textCacheDesc);
        textSampler = samplerBilinear();
        nextFreeCacheY = 0;

        // Premultiplied alpha
        D3D11_BLEND_DESC blendDesc;
        zero(blendDesc);
        auto &blendRt = blendDesc.RenderTarget[0];
        blendRt.BlendEnable    = TRUE;
        blendRt.BlendOp        = D3D11_BLEND_OP_ADD;
        blendRt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        blendRt.SrcBlend       = D3D11_BLEND_ONE;
        blendRt.SrcBlendAlpha  = D3D11_BLEND_ONE;
        blendRt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        blendRt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendRt.RenderTargetWriteMask = 0x0f;

        textPipeline = GraphicsPipeline(
            text_vs, text_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            nullptr,
            &rasterizerDesc(true),
            &blendDesc);
        textPipeline.inputLayout = inputLayoutFor<Vertex>(text_vs);

        D3D11_BUFFER_DESC vbDesc = {0};
        vbDesc.StructureByteStride = sizeof(Vertex);
        vbDesc.ByteWidth      = MaxTexts * 4 * sizeof(Vertex);
        vbDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_BUFFER_DESC ibDesc = {0};
        ibDesc.StructureByteStride = sizeof(uint16_t);
        ibDesc.ByteWidth      = MaxTexts * 4 * 2 * sizeof(uint16_t);
        ibDesc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
        ibDesc.Usage          = D3D11_USAGE_DYNAMIC;
        ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        vertexBuffer = Resource(vbDesc, DXGI_FORMAT_UNKNOWN);
        indexBuffer  = Resource(ibDesc, DXGI_FORMAT_R16_UINT);
    }

    void clear()
    {
        auto numColumns = columns.size();
        columns.clear();
        columns.resize(numColumns);
        nextFreeCacheY = 0;
    }

    void addCallback(unsigned column, int row, TextUpdateCallback updater, float3 color = {1, 1, 1})
    {
        auto &col = columns[column];

        if (row < 0)
            row = static_cast<int>(col.size());

        if (row >= col.size())
            col.resize(row + 1);

        auto &txt = col[row];
        txt.updateText = std::move(updater);
        txt.color = color;
        txt.update();
    }

    void addText(unsigned column, int row, const std::string &text, float3 color = {1, 1, 1})
    {
        addCallback(column, row, [=](TextBuffer &textBuffer)
        {
            sprintf_s(textBuffer, "%s", text.c_str());
        }, color);
    }

    template <typename T>
    void addNumber(unsigned labelColumn, int labelRow, const std::string &label, T &number,
                   float3 labelColor = {1, 1, 1}, float3 numberColor = {1, 1, 1})
    {
        addText(labelColumn, labelRow, label + ":", labelColor);
        addCallback(labelColumn + 1, labelRow, [&number](TextBuffer &textBuffer)
        {
            double d = static_cast<double>(number);
            sprintf_s(textBuffer, "%.3f", d);
        }, numberColor);
    }

    void addNumberCallback(unsigned labelColumn, int labelRow, const std::string &label, std::function<double(void)> number,
                   float3 labelColor = {1, 1, 1}, float3 numberColor = {1, 1, 1})
    {
        addText(labelColumn, labelRow, label + ":", labelColor);
        addCallback(labelColumn + 1, labelRow, [number](TextBuffer &textBuffer)
        {
            double d = number();
            sprintf_s(textBuffer, "%.3f", d);
        }, numberColor);
    }

    template <typename Enum>
    void addEnum(unsigned labelColumn, int labelRow, const std::string &label, Enum &e,
                 float3 labelColor = { 1, 1, 1 }, float3 valueColor = { 1, 1, 1 })
    {
        addText(labelColumn, labelRow, label + ":", labelColor);
        addCallback(labelColumn + 1, labelRow, [&e](TextBuffer &textBuffer)
        {
            auto s = enumToString(e);
            sprintf_s(textBuffer, "%s", s);
        }, valueColor);
    }

    void addBool(unsigned labelColumn, int labelRow, const std::string &label, bool &b,
                 float3 labelColor = { 1, 1, 1 }, float3 valueColor = { 1, 1, 1 })
    {
        addText(labelColumn, labelRow, label + ":", labelColor);
        addCallback(labelColumn + 1, labelRow, [&b](TextBuffer &textBuffer)
        {
            sprintf_s(textBuffer, "%s", b ? "Enabled" : "Disabled");
        }, valueColor);
    }

    void addBlankLine(unsigned column, int row)
    {
        addCallback(column, row, [=](TextBuffer &textBuffer) { textBuffer[0] = '\0'; });
    }

    void updateTextCache()
    {
        GPUScope scope(L"Update text cache");

        for (auto &col : columns)
        {
            for (auto &txt : col)
            {
                bool updateCache = txt.update() || !txt.isCached();

                if (txt.text.empty())
                    continue;

                if (updateCache)
                {
                    auto textPixels = fontRasterizer.renderText(txt.text);

                    if (txt.height == 0)
                    {
                        txt.height = textPixels.height;
                    }
                    else
                    {
                        check(txt.height == textPixels.height, "Text height changed unexpectedly.");
                    }

                    txt.width  = textPixels.width;

                    if (txt.cacheY < 0)
                    {
                        txt.cacheY = nextFreeCacheY;
                        nextFreeCacheY += txt.height;
                        check(nextFreeCacheY <= TextCacheH, "Out of text cache space.");
                    }

                    D3D11_BOX dstBox = {0};
                    dstBox.left   = 0;
                    dstBox.top    = txt.cacheY;
                    dstBox.front  = 0;
                    dstBox.right  = dstBox.left + txt.width;
                    dstBox.bottom = dstBox.top + txt.height;
                    dstBox.back   = 1;
                    context->UpdateSubresource(textCache.texture,
                                               0, &dstBox,
                                               textPixels.pixels.data(),
                                               textPixels.rowPitch(), 0);

                    static const float CacheW = static_cast<float>(TextCacheW);
                    static const float CacheH = static_cast<float>(TextCacheH);

                    txt.uvUpperLeft[0]  = 0;
                    txt.uvUpperLeft[1]  = static_cast<float>(txt.cacheY) / CacheH;
                    txt.uvLowerRight[0] = static_cast<float>(txt.width) / CacheW;
                    txt.uvLowerRight[1] = static_cast<float>(txt.cacheY + txt.height) / CacheH;
                }
            }
        }
    }

    void render(Resource &renderTarget, uint2 offset)
    {
        uint2 coords = offset;

        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;

        size_t numTexts = 0;
        unsigned maxRowHeight = RowMargin;

        for (auto &col : columns)
        {
            numTexts += col.size();
            for (auto &txt : col)
                maxRowHeight = std::max(maxRowHeight, txt.height);
        }

        vertices.reserve(numTexts * 4);
        indices.reserve(numTexts * 6);

        float rtW = static_cast<float>(renderTarget.textureDescriptor().Width);
        float rtH = static_cast<float>(renderTarget.textureDescriptor().Height);

        for (auto &col : columns)
        {
            coords[1] = offset[1];

            unsigned rowHeight;
            unsigned colWidth = 0;

            for (auto &txt : col)
            {
                colWidth = std::max(colWidth, txt.width);

                if (!txt.text.empty())
                {
                    rowHeight = txt.height;

                    float x0UV = static_cast<float>(coords[0])  / rtW;
                    float y0UV = static_cast<float>(coords[1])  / rtH;
                    float x1UV = x0UV + static_cast<float>(txt.width)  / rtW;
                    float y1UV = y0UV + static_cast<float>(txt.height) / rtH;

                    float x0NDC =   x0UV * 2 - 1;
                    float y0NDC = -(y0UV * 2 - 1);
                    float x1NDC =   x1UV * 2 - 1;
                    float y1NDC = -(y1UV * 2 - 1);

                    float H = (y0NDC - y1NDC) / 2.f * rtH;

                    Vertex v0;
                    Vertex v1;
                    Vertex v2;
                    Vertex v3;

                    v0.pos[0] = x0NDC;
                    v0.pos[1] = y0NDC;
                    v1.pos[0] = x0NDC;
                    v1.pos[1] = y1NDC;
                    v2.pos[0] = x1NDC;
                    v2.pos[1] = y0NDC;
                    v3.pos[0] = x1NDC;
                    v3.pos[1] = y1NDC;

                    v0.uv     = txt.uvUpperLeft;
                    v1.uv[0]  = txt.uvUpperLeft[0];
                    v1.uv[1]  = txt.uvLowerRight[1];
                    v2.uv[0]  = txt.uvLowerRight[0];
                    v2.uv[1]  = txt.uvUpperLeft[1];
                    v3.uv     = txt.uvLowerRight;

                    float4 c;
                    c[0] = txt.color[0];
                    c[1] = txt.color[1];
                    c[2] = txt.color[2];
                    c[3] = 1;

                    v0.color = c;
                    v1.color = c;
                    v2.color = c;
                    v3.color = c;

                    uint16_t iBase = static_cast<uint16_t>(vertices.size());
                    uint16_t i0    = iBase + 0;
                    uint16_t i1    = iBase + 1;
                    uint16_t i2    = iBase + 2;
                    uint16_t i3    = iBase + 1;
                    uint16_t i4    = iBase + 3;
                    uint16_t i5    = iBase + 2;

                    vertices.emplace_back(v0);
                    vertices.emplace_back(v1);
                    vertices.emplace_back(v2);
                    vertices.emplace_back(v3);

                    indices.emplace_back(i0);
                    indices.emplace_back(i1);
                    indices.emplace_back(i2);
                    indices.emplace_back(i3);
                    indices.emplace_back(i4);
                    indices.emplace_back(i5);
                }
                else
                {
                    rowHeight = maxRowHeight;
                }

                coords[1] += rowHeight + RowMargin;
            }

            coords[0] += colWidth + ColMargin;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;

        checkHR(context->Map(vertexBuffer.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        memcpy(mapped.pData, vertices.data(), sizeBytes(vertices));
        context->Unmap(vertexBuffer.buffer, 0);

        checkHR(context->Map(indexBuffer.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        memcpy(mapped.pData, indices.data(), sizeBytes(indices));
        context->Unmap(indexBuffer.buffer, 0);

        UINT strides[] = { sizeof(Vertex) };
        UINT offsets[] = { 0 };

        setRenderTarget(renderTarget);

        textPipeline.bind();
        context->IASetVertexBuffers(0, 1, bind(vertexBuffer.buffer), strides, offsets);
        context->IASetIndexBuffer(indexBuffer.buffer, DXGI_FORMAT_R16_UINT, 0);
        context->PSSetShaderResources(0, 1, bind(textCache.srv));
        context->PSSetSamplers(0, 1, bind(textSampler));
        context->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);

        setRenderTarget(nullptr);
    }
};

class SVBRDFCollection
{
    std::string rootPath;
    std::vector<std::string> names;
public:
    SVBRDFCollection() {}

    SVBRDFCollection(const char *rootPath)
        : rootPath(rootPath)
    {
        auto paramsFiles = searchFiles(rootPath, "map_params.dat");
        for (auto &p : paramsFiles)
        {
            auto pathParts = splitPath(p);
            pathParts.pop_back();
            pathParts.pop_back();
            pathParts.pop_back();
            names.emplace_back(pathParts.back());
        }

        log("Found %u SVBRDFs.\n", static_cast<unsigned>(names.size()));
    }

    int size() const
    {
        return static_cast<int>(names.size());
    }

    SVBRDF load(int index) const
    {
        if (names.empty())
            return SVBRDF();

        return loadSVBRDF(rootPath, names[index]);
    }

    int indexOf(const std::string &name) const
    {
        if (name.empty())
            return -1;

        for (int i = 0; i < size(); ++i)
        {
            if (names[i] == name)
                return i;
        }

        return -1;
    }

    bool loadDialog(SVBRDF &svbrdf) const
    {
        auto file = fileOpenDialog("Captured SVBRDF (map_*.pfm)", "map_*.pfm");
        if (!file.empty())
        {
            auto pathParts = splitPath(file);
            pathParts.pop_back(); // filename
            pathParts.pop_back();
            pathParts.pop_back();
            auto name = pathParts.back();
            pathParts.pop_back();
            auto root = join(pathParts.begin(), pathParts.end(), "/");
            svbrdf = loadSVBRDF(root, name);
            return true;
        }
        else
        {
            return false;
        }
    }
};

class MeshCollection
{
    std::vector<std::string> paths;
public:
    MeshCollection() {}

    MeshCollection(const char *rootPath)
    {
        auto paramsFiles = searchFiles(rootPath, "*.obj");

        std::unordered_set<std::string> deduplicatedPaths;
        deduplicatedPaths.reserve(paramsFiles.size());

        for (auto &p : paramsFiles)
        {
            auto pathParts = splitPath(p);
            // last part is the filename, remove it to get just the directory.
            pathParts.pop_back();
            deduplicatedPaths.emplace(join(pathParts.begin(), pathParts.end(), "/"));
        }

        paths = std::vector<std::string>(deduplicatedPaths.begin(), deduplicatedPaths.end());
        std::sort(paths.begin(), paths.end());

        log("Found %u meshes.\n", static_cast<unsigned>(paths.size()));
    }

    int size() const
    {
        return static_cast<int>(paths.size());
    }

    Mesh load(int index, float tessellationTriangleArea = 0) const
    {
        if (paths.empty())
            return Mesh();

        auto meshFiles = searchFiles(paths[index], "*.obj");
        return loadMesh(meshFiles, MeshLoadMode::SwapYZ, tessellationTriangleArea);
    }

    int indexOf(const std::string &name) const
    {
        if (name.empty())
            return -1;

        for (int i = 0; i < size(); ++i)
        {
            if (paths[i].find_first_of(name, 0) != std::string::npos)
                return i;
        }

        return -1;
    }

    bool loadDialog(Mesh &mesh, float tessellationTriangleArea = 0) const
    {
        auto file = fileOpenDialog("Wavefront .OBJ", "*.obj");
        if (!file.empty())
        {
            auto pathParts = splitPath(file);
            pathParts.pop_back();
            auto meshFiles = searchFiles(join(pathParts.begin(), pathParts.end(), "/"), "*.obj");
            mesh = loadMesh(meshFiles, MeshLoadMode::SwapYZ, tessellationTriangleArea);
            return true;
        }
        else
        {
            return false;
        }
    }

    static void retessellate(Mesh &mesh, float tessellationTriangleArea = 0)
    {
        Mesh newMesh = loadMesh(mesh.objFiles, MeshLoadMode::SwapYZ, tessellationTriangleArea);
        if (newMesh.valid())
            mesh = newMesh;
        else
            log("Could not reload mesh for re-tessellation.\n");
    }
};

float computeTargetTriangleArea(const SVBRDF &svbrdf, float displacementDensity)
{
    float xDim = static_cast<float>(svbrdf.width);
    float yDim = static_cast<float>(svbrdf.height);

    float targetX = xDim / displacementDensity;
    float targetY = yDim / displacementDensity;
    float targetU = 1 / targetX;
    float targetV = 1 / targetY;

    float targetTriangleArea = targetU * targetV / 2;

    return targetTriangleArea;
}

class SVBRDFRenderer final 
{
public:
    struct Constants
    {
        XMMATRIX viewProj;
        XMVECTOR ambientLight;
        XMVECTOR cameraPosition;
        TonemapMode tonemapMode;
        float maxLuminance;
        uint  normalMode;
        uint  useNormalMapping;
        float displacementDensity;
        float displacementMagnitude;
        uint  shadowLights;
        uint  shadowResolution;
        uint  shadowPcfTaps;
        float shadowKernelWidth;
        int   shadowDepthBias;
        float shadowSSDepthBias;
        bool  wireframe;
        bool  tessellation;
    };

private:
    GraphicsPipeline renderMeshPipeline;
    GraphicsPipeline renderMeshPipelineTessellated;
    Resource vertexBuffer;
    Resource indexBuffer;
    CComPtr<ID3D11SamplerState> bilinear;
    CComPtr<ID3D11SamplerState> aniso;

    MeshMode meshMode;
    DisplacementMode displacementMode;
    unsigned indexCount;
    float meshScale;

    std::vector<Light> lights;

    struct RegularMeshVSConstants
    {
        XMMATRIX viewProj;
        float scale;
        float displacementMagnitude;
    };

    struct LightingPSConstants
    {
        XMVECTOR ambientLight;
        XMVECTOR cameraPosition;
        float  alpha;
        uint   tonemapMode;
        float  maxLuminance;
        uint   normalMode;
        uint   useNormalMapping;
        uint   numLights;
    };

    struct TextureSpacePSConstants
    {
        float displacementMagnitude;
    };

    LightingMode lightingMode;
    TextureSpaceLightingPrecision lightingPrecision;
    GraphicsPipeline renderTextureSpaceLightingPipeline;
    Resource textureSpaceLightingMap;
    Resource textureSpaceLightingDepth;

    Resource lightBuffer;

    struct ShadowConstants
    {
        uint  shadowLights;
        uint  shadowPcfTaps;
        float shadowKernelWidth;
        float shadowMapResolution;
    };

    struct ShadowUnprojectConstants
    {
        XMMATRIX viewProj;
        XMMATRIX shadowInvViewProj;
        uint slice;
        uint resolution;
    };

    unsigned shadowLights;
    GraphicsPipeline renderShadowMapPipeline;
    GraphicsPipeline renderShadowMapPipelineTessellated;
    GraphicsPipeline unprojectShadowMapPipeline;
    Resource shadowMaps;
    std::vector<XMMATRIX> shadowViewProjs;
    Resource shadowViewProjBuffer;
    CComPtr<ID3D11SamplerState> shadowSampler;
    std::vector<Resource> shadowMapCubeFaceDSVs;
    ShadowConstants shadowConstants;
    Resource debugRTV;

public:

    SVBRDFRenderer(MeshMode meshMode, DisplacementMode displacementMode,
                   LightingMode lightingMode,
                   TextureSpaceLightingPrecision lightingPrecision)
        : meshMode(meshMode)
        , displacementMode(displacementMode)
        , lightingMode(lightingMode)
        , lightingPrecision(lightingPrecision)
    {
        if (lightingMode == LightingMode::ForwardLighting)
        {
            constructForward();
        }
        else if (lightingMode == LightingMode::TextureSpaceLighting)
        {
            constructTextureSpace();
        }
        else
        {
            check(false, "Unknown lighting mode!");
        }

        constructLightBuffer();

        bilinear = samplerBilinear(D3D11_TEXTURE_ADDRESS_WRAP);
        aniso = samplerAnisotropic(8, D3D11_TEXTURE_ADDRESS_WRAP);

        indexCount = 0;
        meshScale = 1;
    }

    void init(SVBRDF &svbrdf, Mesh *mesh, const Constants &constants) 
    {
        static const float Dim = 5.f;

        float displacementDensity   = constants.displacementDensity;
        float displacementMagnitude = constants.displacementMagnitude;

        float smallerDim = static_cast<float>(std::min(svbrdf.width, svbrdf.height));
        float xDim = Dim * static_cast<float>( svbrdf.width) / smallerDim;
        float yDim = Dim * static_cast<float>(svbrdf.height) / smallerDim;

        bool displacementEnabled =
            displacementMode != DisplacementMode::NoDisplacement
            && displacementDensity > 0
            && displacementMagnitude != 0;

        if (meshMode == MeshMode::SingleQuad)
        {
            if (displacementEnabled)
            {
                if (displacementMode == DisplacementMode::CPUDisplacementMapping)
                {
                    initCPUDisplacementMapped(svbrdf, xDim, yDim, displacementDensity, displacementMagnitude, 1.f);
                }
                else
                {
                    float cpuTess = 64.f;
                    float targetTriangleArea = computeTargetTriangleArea(svbrdf, displacementDensity);
                    float uDim = xDim / cpuTess;
                    float vDim = yDim / cpuTess;
                    float cpuTriangleArea = uDim * vDim / 2;
                    float areaRatio = cpuTriangleArea / targetTriangleArea;
                    float gpuTess = std::sqrt(areaRatio);
                    initCPUDisplacementMapped(svbrdf, xDim, yDim, cpuTess, 0, gpuTess);
                }
            }
            else
            {
                initSingleQuad(svbrdf, xDim, yDim, MaxTessellation);
            }
        }
        else if (meshMode == MeshMode::LoadedMesh)
        {
            initLoadedMesh(svbrdf, *mesh, Dim);
        }
        else
        {
            check(false, "Unknown mesh mode!");
        }

        if (lightingMode == LightingMode::TextureSpaceLighting)
        {
            DXGI_FORMAT format;

            switch (lightingPrecision)
            {
            default:
            case TextureSpaceLightingPrecision::Float11_11_10:
                format = DXGI_FORMAT_R11G11B10_FLOAT;
                break;
            case TextureSpaceLightingPrecision::Float16:
                format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                break;
            case TextureSpaceLightingPrecision::Float32:
                format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }
            auto lightingMapDesc = texture2DDesc(svbrdf.width, svbrdf.height, format);
            auto dimPow2 = std::min(roundUpToPowerOf2(svbrdf.width), roundUpToPowerOf2(svbrdf.height));
            lightingMapDesc.MipLevels = static_cast<UINT>(log2(dimPow2));
            lightingMapDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            lightingMapDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            textureSpaceLightingMap = Resource(lightingMapDesc);
            RESOURCE_DEBUG_NAME(textureSpaceLightingMap);
        }

        constructShadowMapping(constants);
    }

    void updateLights(const std::vector<Light> &newLights)
    {
        lights.resize(newLights.size());
        std::copy(newLights.begin(), newLights.end(), lights.begin());

        D3D11_BOX dstBox = {0};
        dstBox.left   = 0;
        dstBox.right  = static_cast<UINT>(sizeBytes(lights));
        dstBox.top    = 0;
        dstBox.bottom = 1;
        dstBox.front  = 0;
        dstBox.back   = 1;

        context->UpdateSubresource(lightBuffer.buffer, 0, &dstBox,
                                   lights.data(),
                                   dstBox.right, 0);

        if (!shadowViewProjs.empty())
        {
            for (unsigned L = 0; L < shadowLights; ++L)
            {
                for (unsigned i = 0; i < 6; ++i)
                {
                    unsigned idx = L * 6 + i;
                    shadowViewProjs.at(idx) = computeShadowViewProj(L, i);
                }
            }

            dstBox.right = static_cast<UINT>(sizeBytes(shadowViewProjs));

            context->UpdateSubresource(shadowViewProjBuffer.buffer, 0, &dstBox,
                                       shadowViewProjs.data(),
                                       dstBox.right, 0);
        }
    }

    void renderViewportIndependent(ConstantBuffers &cb,
                                   SVBRDF &svbrdf,
                                   const Constants &constants) 
    {
        shadowConstants = computeShadowConstants(constants);

        if (shadowLights > 0)
        {
            renderShadowMaps(cb, svbrdf, constants);
        }
    }

    void render(ConstantBuffers &cb,
                SVBRDF &svbrdf,
                const Constants &konstants,
                Resource &renderTarget, Resource &depthBuffer) 
    {
        Constants constants = konstants;
#if defined(DEBUG_SHADOW_MATRICES)
        static int shadowView = -1;
        static int prevShadowView = -1;
        updateValueWrap('E', 'Q', shadowView, 1, -1, 6);
        if (shadowView >= 0 && !shadowViewProjs.empty())
        {
            constants.viewProj = shadowViewProjs[shadowView];

            if (prevShadowView != shadowView)
            {
                log("Shadow view: %d\n", shadowView);
                prevShadowView = shadowView;
            }
        }
#endif

        shadowConstants = computeShadowConstants(constants);

        if (lightingMode == LightingMode::ForwardLighting)
        {
            renderForward(cb, svbrdf, constants, renderTarget, depthBuffer);
        }
        else if (lightingMode == LightingMode::TextureSpaceLighting)
        {
            renderTextureSpaceLighting(cb, svbrdf, constants, renderTarget, depthBuffer);
        }
    }

    void unprojectShadowMap(ConstantBuffers &cb, const Constants &constants, unsigned slice)
    {
        GPUScope scope(L"unprojectShadowMap");

        ShadowUnprojectConstants vsConstants;
        vsConstants.viewProj          = constants.viewProj;
        vsConstants.shadowInvViewProj = XMMatrixInverse(nullptr, shadowViewProjs[slice]);
        vsConstants.resolution        = constants.shadowResolution;
        vsConstants.slice             = slice;

        auto vsCB = cb.write(vsConstants);

        unprojectShadowMapPipeline.bind();
        context->VSSetShaderResources(0, 1, bind(shadowMaps.srv));
        context->VSSetConstantBuffers(0, 1, bind(vsCB));
        context->Draw(constants.shadowResolution * constants.shadowResolution, 0);
        unbindResources(&ID3D11DeviceContext::VSSetShaderResources, { 0 });
    }

private:
    void constructForward()
    {
        // Setup depth buffering with inverse Z
        renderMeshPipeline = GraphicsPipeline(
            regularmesh_vs,
            regularlighting_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &rasterizerDesc(true));
        renderMeshPipeline.psWireframe = Shader<PS>(wireframe_ps);

        renderMeshPipeline.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);

        renderMeshPipelineTessellated = GraphicsPipeline(
            regularmesh_vs,
            displacement_hs,
            displacement_ds,
            regularlighting_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &rasterizerDesc(true));
        renderMeshPipelineTessellated.psWireframe = Shader<PS>(wireframe_ps);

        renderMeshPipelineTessellated.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);
    }

    void constructTextureSpace()
    {
        // Setup depth buffering with inverse Z
        renderMeshPipeline = GraphicsPipeline(
            regularmesh_vs,
            samplelightingfromtexture_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &rasterizerDesc(true));
        renderMeshPipeline.psWireframe = Shader<PS>(wireframe_ps);

        renderMeshPipelineTessellated = GraphicsPipeline(
            regularmesh_vs,
            displacement_hs,
            displacement_ds,
            samplelightingfromtexture_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &rasterizerDesc(true));
        renderMeshPipelineTessellated.psWireframe = Shader<PS>(wireframe_ps);

        renderTextureSpaceLightingPipeline = GraphicsPipeline(
            texturespacemesh_vs,
            texturespacelighting_ps,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            &depthStencilDesc(DepthMode::InverseDepth, true),
            &rasterizerDesc(true));

        renderMeshPipeline.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);
        renderMeshPipelineTessellated.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);
        renderTextureSpaceLightingPipeline.inputLayout = inputLayoutFor<Vertex>(texturespacemesh_vs);
    }

    void constructLightBuffer()
    {
        D3D11_BUFFER_DESC desc;
        zero(desc);
        desc.ByteWidth           = MaxLights * sizeof(Light);
        desc.StructureByteStride = sizeof(Light);
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.MiscFlags  = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        lightBuffer = Resource(desc, DXGI_FORMAT_UNKNOWN);
        RESOURCE_DEBUG_NAME(lightBuffer);
    }

    void constructShadowMapping(const Constants &constants)
    {
        shadowLights = constants.shadowLights;

        log("Shadow resolution %u x %u, bias (%d, %4.2f)\n",
            constants.shadowResolution,
            constants.shadowResolution,
            constants.shadowDepthBias,
            constants.shadowSSDepthBias);

        {
            renderShadowMapPipeline = GraphicsPipeline(
                regularmesh_vs,
#if defined(DEBUG_SHADOW_MAPS)
                debugshadowmap_ps,
#else
                nullptr,
#endif
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                //&depthStencilDesc(DepthMode::InverseDepth, true, true),
                &depthStencilDesc(DepthMode::InverseDepth, true),
                &rasterizerDesc(true,
                                constants.shadowDepthBias,
                                constants.shadowSSDepthBias));

            renderShadowMapPipeline.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);
            renderShadowMapPipeline.vs = renderMeshPipeline.vs;
        }

        {
            renderShadowMapPipelineTessellated = GraphicsPipeline(
                regularmesh_vs,
                displacement_hs,
                displacement_ds,
#if defined(DEBUG_SHADOW_MAPS)
                debugshadowmap_ps,
#else
                nullptr,
#endif
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                &depthStencilDesc(DepthMode::InverseDepth, true),
                &rasterizerDesc(true,
                                constants.shadowDepthBias,
                                constants.shadowSSDepthBias));

            renderShadowMapPipelineTessellated.inputLayout = inputLayoutFor<Vertex>(regularmesh_vs);
        }

        {
            unprojectShadowMapPipeline = GraphicsPipeline(
                unprojectshadowmap_vs,
                wireframe_ps,
                D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
                &depthStencilDesc(DepthMode::Always, false, false));
        }

        UINT faces = static_cast<UINT>(6 * shadowLights);

        auto shadowDesc = texture2DDesc(constants.shadowResolution, constants.shadowResolution, DXGI_FORMAT_D32_FLOAT);
        shadowDesc.ArraySize = std::max(6u, faces);
        shadowDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        shadowViewProjs.clear();
        shadowViewProjs.resize(shadowLights * 6);

        shadowMapCubeFaceDSVs.clear();

        shadowMaps = Resource(shadowDesc);
        RESOURCE_DEBUG_NAME(shadowMaps);
        for (unsigned L = 0; L < shadowLights; ++L)
        {
            for (unsigned i = 0; i < 6; ++i)
            {
                D3D11_DEPTH_STENCIL_VIEW_DESC faceDesc;
                zero(faceDesc);
                faceDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                faceDesc.Format        = DXGI_FORMAT_D32_FLOAT;
                faceDesc.Texture2DArray.MipSlice        = 0;
                faceDesc.Texture2DArray.ArraySize       = 1;
                faceDesc.Texture2DArray.FirstArraySlice = L * 6 + i;

                shadowMapCubeFaceDSVs.emplace_back();
                auto &face = shadowMapCubeFaceDSVs.back();
                face = Resource(shadowMaps);
                face.views(nullptr, nullptr, nullptr, &faceDesc);
            }
        }

        D3D11_BUFFER_DESC viewProjDesc;
        zero(viewProjDesc);
        viewProjDesc.ByteWidth = static_cast<UINT>(std::max(sizeof(XMMATRIX), sizeBytes(shadowViewProjs)));
        viewProjDesc.StructureByteStride = sizeof(XMMATRIX);
        viewProjDesc.Usage = D3D11_USAGE_DEFAULT;
        viewProjDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        viewProjDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        shadowViewProjBuffer = Resource(viewProjDesc, DXGI_FORMAT_UNKNOWN);
        RESOURCE_DEBUG_NAME(shadowViewProjBuffer);

        auto debugDesc = texture2DDesc(constants.shadowResolution, constants.shadowResolution, DXGI_FORMAT_R32G32B32A32_FLOAT);
        debugDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        debugRTV = Resource(debugDesc);

#if defined(SHADOW_USE_COMPARISON_SAMPLER)
        D3D11_SAMPLER_DESC pcfDesc;
        zero(pcfDesc);
        pcfDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        pcfDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        pcfDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        pcfDesc.Filter   = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        // Because we're using inverse Z, the shadow test passes if the
        // test value is >= the shadow map value.
        pcfDesc.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL;
        pcfDesc.MinLOD   = 0;
        pcfDesc.MaxLOD   = D3D11_FLOAT32_MAX;
        shadowSampler = nullptr;
        checkHR(device->CreateSamplerState(&pcfDesc, &shadowSampler));
#else
        shadowSampler = samplerPoint();
#endif
    }

    void initSingleQuad(SVBRDF &svbrdf, float xDim, float yDim, float tessellation)
    {
        // Counterclockwise single quad
        Vertex vertices[] = {
            { { -xDim,  yDim, 0, }, { 0, 0, 1, }, { 0, 0 }, tessellation },
            { {  xDim,  yDim, 0, }, { 0, 0, 1, }, { 1, 0 }, tessellation },
            { { -xDim, -yDim, 0, }, { 0, 0, 1, }, { 0, 1 }, tessellation },
            { {  xDim, -yDim, 0, }, { 0, 0, 1, }, { 1, 1 }, tessellation },
        };
        uint32_t indices[] = {
            0, 2, 1,
            1, 2, 3,
        };

        D3D11_BUFFER_DESC vbDesc;
        zero(vbDesc);
        vbDesc.ByteWidth = sizeof(vertices);
        vbDesc.StructureByteStride = sizeof(Vertex);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
        vertexBuffer     = Resource(vbDesc, DXGI_FORMAT_UNKNOWN, vertices, sizeof(vertices));
        RESOURCE_DEBUG_NAME(vertexBuffer);

        D3D11_BUFFER_DESC ibDesc;
        zero(ibDesc);
        ibDesc.ByteWidth = sizeof(indices);
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
        indexBuffer      = Resource(ibDesc, DXGI_FORMAT_R32_UINT, indices, sizeof(indices));
        indexCount       = static_cast<UINT>(::size(indices));
        RESOURCE_DEBUG_NAME(indexBuffer);

        meshScale = 1;
    }

    void initLoadedMesh(SVBRDF &svbrdf, Mesh &mesh, float dim)
    {
        vertexBuffer = mesh.vertexBuffer;
        RESOURCE_DEBUG_NAME(vertexBuffer);
        indexBuffer  = mesh.indexBuffer;
        RESOURCE_DEBUG_NAME(indexBuffer);
        indexCount   = mesh.indexAmount;

        // set the mesh scale so that the furthest away vertex is at distance 'dim'
        meshScale    = dim / mesh.scale;
    }

    // Always compile CPU displacement mapping with optimizations enabled, even in debug mode.
    void initCPUDisplacementMapped(SVBRDF &svbrdf, float xDim, float yDim, float displacementDensity, float displacementMagnitude, float tessellation = 1)
    {
        unsigned pixelsPerVertex = std::max(1u, static_cast<unsigned>(std::ceil(displacementDensity)));
        pixelsPerVertex = std::min(pixelsPerVertex, 64u);

        if (svbrdf.heightMapCPU.width <= 0 || svbrdf.heightMapCPU.height <= 0)
        {
            log("No heightmap for \"%s\", using a single quad instead.\n",
                svbrdf.name.c_str());
            initSingleQuad(svbrdf, xDim, yDim, MaxTessellation);
            return;
        }
        else if (displacementDensity < 1)
        {
            log("Displacement density set to no-op, using a single quad instead.\n",
                svbrdf.name.c_str());
            initSingleQuad(svbrdf, xDim, yDim, MaxTessellation);
            return;
        }

        Timer t;

        const unsigned W = svbrdf.width  / pixelsPerVertex;
        const unsigned H = svbrdf.height / pixelsPerVertex;
        const unsigned numVerts = W * H;
        const unsigned quadsX = W - 1;
        const unsigned quadsY = H - 1;
        const unsigned numQuads = quadsX * quadsY;
        const unsigned numTriangles = numQuads * 2;

        std::vector<Vertex> vertices;
        vertices.reserve(W * H);
        std::vector<uint32_t> indices;
        indices.reserve(numTriangles * 3);

        const float maxX = static_cast<float>(W - 1);
        const float maxY = static_cast<float>(H - 1);
        for (unsigned y = 0 ; y < H; ++y)
        {
            for (unsigned x = 0 ; x < W; ++x)
            {
                float height = svbrdf.heightMapCPU(x * pixelsPerVertex, y * pixelsPerVertex, 0); 

                float u = static_cast<float>(x) / maxX;
                float v = static_cast<float>(y) / maxY;

                float vx = ((       u  * 2) - 1) * xDim;
                float vy = (((1.f - v) * 2) - 1) * yDim;
                float vz = height * displacementMagnitude;

                Vertex vert;
                vert.pos[0] = vx;
                vert.pos[1] = vy;
                vert.pos[2] = vz;
                vert.uv[0] = u;
                vert.uv[1] = v;
                vert.tessellation = tessellation;
                vertices.emplace_back(vert);
            }
        }

        for (unsigned qy = 0; qy < quadsY; ++qy)
        {
            for (unsigned qx = 0; qx < quadsX; ++qx)
            {
                bool even = ((qy + qx) % 2) == 0;

                // even quads:
                // A---B
                // |  /|
                // | / |
                // |/  |
                // C---D
                // odd quads:
                // A---B
                // |\  |
                // | \ |
                // |  \|
                // C---D
                //
                // where
                // A = (qx,     qy    )
                // B = (qx + 1, qy    )
                // C = (qx    , qy + 1)
                // D = (qx + 1, qy + 1)

                uint32_t Ax = qx;
                uint32_t Ay = qy;
                uint32_t Bx = qx + 1;
                uint32_t By = qy;
                uint32_t Cx = qx;
                uint32_t Cy = qy + 1;
                uint32_t Dx = qx + 1;
                uint32_t Dy = qy + 1;

                uint32_t A = Ay * W + Ax;
                uint32_t B = By * W + Bx;
                uint32_t C = Cy * W + Cx;
                uint32_t D = Dy * W + Dx;

                // Counterclockwise triangles
                if (even)
                {
                    indices.emplace_back(A);
                    indices.emplace_back(C);
                    indices.emplace_back(B);

                    indices.emplace_back(B);
                    indices.emplace_back(C);
                    indices.emplace_back(D);
                }
                else
                {
                    indices.emplace_back(A);
                    indices.emplace_back(D);
                    indices.emplace_back(B);

                    indices.emplace_back(A);
                    indices.emplace_back(C);
                    indices.emplace_back(D);
                }
            }
        }

        computeVertexNormals(vertices, indices);

        D3D11_BUFFER_DESC vbDesc;
        zero(vbDesc);
        vbDesc.ByteWidth = static_cast<UINT>(sizeBytes(vertices));
        vbDesc.StructureByteStride = sizeof(Vertex);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
        vertexBuffer     = Resource(vbDesc, DXGI_FORMAT_UNKNOWN, vertices.data(), sizeBytes(vertices));
        RESOURCE_DEBUG_NAME(vertexBuffer);

        D3D11_BUFFER_DESC ibDesc;
        zero(ibDesc);
        ibDesc.ByteWidth = static_cast<UINT>(sizeBytes(indices));
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
        indexBuffer      = Resource(ibDesc, DXGI_FORMAT_R32_UINT, indices.data(), sizeBytes(indices));
        indexCount       = static_cast<UINT>(indices.size());
        RESOURCE_DEBUG_NAME(indexBuffer);

        meshScale = 1;

        log("Displacement mapped \"%s\" with PPV = %u and height = %.3f in %.2f ms.\n", svbrdf.name.c_str(), pixelsPerVertex, displacementMagnitude, t.seconds() * 1000);
    }

    XMMATRIX computeShadowViewProj(unsigned light, unsigned faceIndex)
    {
        auto face = static_cast<CubeMapFace>(faceIndex);

        auto pos  = toVec(lights[light].positionWorld, 1);
        auto view = cubeMapFaceViewRH(face, pos);
        auto proj = cubeMapFaceProjRH(ShadowNearZ, ShadowFarZ, DepthMode::InverseDepth);

        XMMATRIX shadowViewProj = XMMatrixMultiply(view, proj);
        return shadowViewProj;
    }
    
    ShadowConstants computeShadowConstants(const Constants &constants)
    {
        ShadowConstants c;
        zero(c);

        c.shadowLights         = shadowLights;
        c.shadowPcfTaps        = constants.shadowPcfTaps;
        c.shadowKernelWidth    = constants.shadowKernelWidth;
        c.shadowMapResolution  = static_cast<float>(constants.shadowResolution);

        return c;
    }

    void renderShadowMaps(ConstantBuffers &cb,
                          SVBRDF &svbrdf,
                          const Constants &constants)
    {
        GPUScope scope(L"renderShadowMaps");

        context->ClearDepthStencilView(shadowMaps.dsv, D3D11_CLEAR_DEPTH, D3D11_MIN_DEPTH, 0);

        if (constants.tessellation)
            renderShadowMapPipelineTessellated.bind();
        else
            renderShadowMapPipeline.bind();

        setVertexBuffers(&vertexBuffer, &indexBuffer);

        for (unsigned L = 0; L < shadowLights; ++L)
        {
            GPUScope scope(L"Point light shadows");
            for (unsigned i = 0; i < 6; ++i)
            {
                GPUScope scope(L"Cube map face");

                auto face = static_cast<CubeMapFace>(i);

                unsigned idx = L * 6 + i;

                RegularMeshVSConstants vsConstants;
                vsConstants.viewProj              = shadowViewProjs[idx];
                vsConstants.scale                 = meshScale;
                vsConstants.displacementMagnitude = constants.displacementMagnitude;

                auto &dsv = shadowMapCubeFaceDSVs[idx];
    #if defined(DEBUG_SHADOW_MAPS)
                context->ClearRenderTargetView(debugRTV.rtv, std::array<float, 4> { 0, 0, 0, 1 }.data());
                setRenderTarget(debugRTV, &dsv);
    #else
                setDepthOnly(dsv);
    #endif

                auto vsCB = cb.write(vsConstants);

                context->VSSetConstantBuffers(0, 1, bind(vsCB));
                context->DSSetConstantBuffers(0, 1, bind(vsCB));
                context->DSSetShaderResources(0, 1, bind(svbrdf.heightMap.srv));
                context->DSSetSamplers(0, 1, bind(bilinear));

                bindLightingResources(svbrdf, false);
    #if defined(DEBUG_SHADOW_MAPS)
                context->PSSetShaderResources(6, 1, bind(shadowViewProjBuffer.srv));
    #endif
                context->DrawIndexed(indexCount, 0, 0);
                unbindLightingResources();

                setRenderTarget(nullptr);
            }
        }
    }

    LightingPSConstants lightingPSConstants(const SVBRDF &svbrdf, const Constants &constants)
    {
        LightingPSConstants psConstants;

        zero(psConstants);

        psConstants.ambientLight       = constants.ambientLight;
        psConstants.cameraPosition     = constants.cameraPosition;
        psConstants.alpha              = svbrdf.alpha;
        psConstants.tonemapMode        = static_cast<uint>(constants.tonemapMode);
        psConstants.maxLuminance       = constants.maxLuminance;
        psConstants.normalMode         = constants.normalMode;
        psConstants.useNormalMapping   = constants.useNormalMapping;
        psConstants.numLights          = static_cast<uint>(lights.size());

        return psConstants;
    }

    void bindLightingResources(SVBRDF &svbrdf, bool bindShadows = true)
    {
        context->PSSetShaderResources(0, 1, bind(svbrdf.diffuseAlbedo.srv));
        context->PSSetShaderResources(1, 1, bind(svbrdf.specularAlbedo.srv));
        context->PSSetShaderResources(2, 1, bind(svbrdf.specularShape.srv));
        context->PSSetShaderResources(3, 1, bind(svbrdf.normals.srv));
        context->PSSetShaderResources(4, 1, bind(lightBuffer.srv));
        context->PSSetSamplers(0, 1, bind(bilinear));

        if (bindShadows)
        {
            context->PSSetShaderResources(5, 1, bind(shadowMaps.srv));
            context->PSSetShaderResources(6, 1, bind(shadowViewProjBuffer.srv));
            context->PSSetSamplers(1, 1, bind(shadowSampler));
        }
    }

    void unbindLightingResources()
    {
        ID3D11ShaderResourceView *nilSRV[7] = { nullptr };
        ID3D11SamplerState *nilSmp[2] = { nullptr };

        context->PSSetShaderResources(0, 7, nilSRV);
        context->PSSetSamplers(0, 2, nilSmp);
    }

    void renderForward(ConstantBuffers &cb,
                       SVBRDF &svbrdf,
                       const Constants &constants,
                       Resource &renderTarget, Resource &depthBuffer)
    {
        GPUScope scope(L"renderForward");

        RegularMeshVSConstants vsConstants;
        vsConstants.viewProj              = constants.viewProj;
        vsConstants.scale                 = meshScale;
        vsConstants.displacementMagnitude = constants.displacementMagnitude;
        LightingPSConstants psConstants = lightingPSConstants(svbrdf, constants);

        setRenderTarget(renderTarget, &depthBuffer);

        if (constants.tessellation)
            renderMeshPipelineTessellated.bind();
        else
            renderMeshPipeline.bind();

        auto vsCB  = cb.write(vsConstants);
        auto psCB0 = cb.write(psConstants);
        auto psCB1 = cb.write(shadowConstants);

        setVertexBuffers(&vertexBuffer, &indexBuffer);

        context->VSSetConstantBuffers(0, 1, bind(vsCB));
        context->DSSetConstantBuffers(0, 1, bind(vsCB));
        context->DSSetShaderResources(0, 1, bind(svbrdf.heightMap.srv));
        context->DSSetSamplers(0, 1, bind(bilinear));

        context->PSSetConstantBuffers(0, 1, bind(psCB0));
        context->PSSetConstantBuffers(1, 1, bind(psCB1));

        bindLightingResources(svbrdf);
        context->DrawIndexed(indexCount, 0, 0);

        if (constants.wireframe)
        {
            GPUScope wf(L"Wireframe");
            if (constants.tessellation)
                renderMeshPipelineTessellated.bindWireframe();
            else
                renderMeshPipeline.bindWireframe();

            context->DrawIndexed(indexCount, 0, 0);
        }

        unbindLightingResources();

        setRenderTarget(nullptr);
    }

    void renderTextureSpaceLighting(ConstantBuffers &cb,
                       SVBRDF &svbrdf,
                       const Constants &constants,
                       Resource &renderTarget, Resource &depthBuffer)
    {
        GPUScope scope(L"renderTextureSpaceLighting");

        LightingPSConstants psConstants = lightingPSConstants(svbrdf, constants);

        {
            GPUScope scope(L"Texture space lighting");

            RegularMeshVSConstants vsConstants;
            TextureSpacePSConstants psDisplacement;
            psDisplacement.displacementMagnitude = constants.displacementMagnitude;

            vsConstants.viewProj = constants.viewProj;
            vsConstants.scale    = meshScale;

            float zero[4] = { 0, 0, 0, 1 };
            context->ClearRenderTargetView(textureSpaceLightingMap.rtv, zero);
            setRenderTarget(textureSpaceLightingMap);

            renderTextureSpaceLightingPipeline.bind();
            auto vsCB = cb.write(vsConstants);
            auto psCB0 = cb.write(psConstants);
            auto psCB1 = cb.write(shadowConstants);
            auto psCB2 = cb.write(psDisplacement);

            setVertexBuffers(&vertexBuffer, &indexBuffer);

            context->VSSetConstantBuffers(0, 1, bind(vsCB));

            context->PSSetConstantBuffers(0, 1, bind(psCB0));
            context->PSSetConstantBuffers(1, 1, bind(psCB1));
            context->PSSetConstantBuffers(2, 1, bind(psCB2));
            context->PSSetShaderResources(7, 1, bind(svbrdf.heightMap.srv));
            context->PSSetSamplers(2, 1, bind(bilinear));

            bindLightingResources(svbrdf);
            context->DrawIndexed(indexCount, 0, 0);
            unbindLightingResources();

            setRenderTarget(nullptr);

            context->GenerateMips(textureSpaceLightingMap.srv);
        }

        {
            GPUScope scope(L"Render with texture space lighting");

            RegularMeshVSConstants vsConstants;

            vsConstants.viewProj              = constants.viewProj;
            vsConstants.scale                 = meshScale;
            vsConstants.displacementMagnitude = constants.displacementMagnitude;

            setRenderTarget(renderTarget, &depthBuffer);

            if (constants.tessellation)
                renderMeshPipelineTessellated.bind();
            else
                renderMeshPipeline.bind();

            auto vsCB = cb.write(vsConstants);
            auto psCB = cb.write(psConstants);

            setVertexBuffers(&vertexBuffer, &indexBuffer);

            context->VSSetConstantBuffers(0, 1, bind(vsCB));

            context->DSSetConstantBuffers(0, 1, bind(vsCB));
            context->DSSetShaderResources(0, 1, bind(svbrdf.heightMap.srv));
            context->DSSetSamplers(0, 1, bind(bilinear));

            context->PSSetConstantBuffers(0, 1, bind(psCB));
            context->PSSetShaderResources(0, 1, bind(textureSpaceLightingMap.srv));
            context->PSSetSamplers(0, 1, bind(aniso));
            context->DrawIndexed(indexCount, 0, 0);

            if (constants.wireframe)
            {
                GPUScope wf(L"Wireframe");
                if (constants.tessellation)
                    renderMeshPipelineTessellated.bindWireframe();
                else
                    renderMeshPipeline.bindWireframe();

                context->DrawIndexed(indexCount, 0, 0);
            }

            unbindResources(&ID3D11DeviceContext::PSSetShaderResources, { 0 });

            setRenderTarget(nullptr);
        }
    }

};

static UINT maximumMSAAQualityFor(DXGI_FORMAT format, UINT count)
{
    UINT maxQuality = 0;
    checkHR(device->CheckMultisampleQualityLevels(format, count, &maxQuality));
    return (maxQuality == 0) ? 0 : (maxQuality - 1);
}

class SVBRDFOculus
{
    Oculus &oculus;
    ConstantBuffers cb;
    LightIndicator lightIndicator;
    TextManager textManager;
    bool showHelp;
    bool wireframe;

    std::string dataDirectory;
    bool rwPresets;
    RenderingState state;

    bool useOculus;

    std::shared_ptr<SVBRDFRenderer> renderer;

    MeshMode meshMode;
    DisplacementMode displacementMode;
    LightingMode lightingMode;
    TextureSpaceLightingPrecision lightingPrecision;
    NormalMode normalMode;
    bool useNormalMapping;

    ShadowMode shadowMode;
    int shadowPcfTaps;
    float shadowKernelWidth;

    FPSCamera camera;

    float lightIntensity;
    int selectedLight;

    SVBRDFCollection materials;
    SVBRDF activeMaterial;
    int materialIndex;

    MeshCollection meshes;
    Mesh activeMesh;
    int meshIndex;

    std::array<Resource, 2> aaTargets;
    std::array<Resource, 2> aaDepth;
public:
    SVBRDFOculus(Oculus &oculus, const std::string &dataDir = std::string(), bool rwPresets = false)
        : oculus(oculus)
        , textManager(3)
        , dataDirectory(dataDir)
        , camera(CameraButtons, XMVectorZero(), 0, 0)
        , rwPresets(rwPresets)
    {
        useOculus = false;
        wireframe = false;

        if (dataDirectory.empty())
            dataDirectory = "data";

        log("Using data directory \"%s\" (%s).\n", dataDirectory.c_str(), absolutePath(dataDirectory).c_str());

        materials = SVBRDFCollection(dataDirectory.c_str());
        materialIndex = 0;

        meshes = MeshCollection(dataDirectory.c_str());
        meshIndex = 0;

        selectedLight = 0;

        RenderingState s;
        s.load(dataDirectory + "/" + QuickPresetFilenames[0]);
        setState(s);

        initHelpText();
        showHelp = true;
    }

    void initHelpText()
    {
        float3 normalText = { 1, 1, 1 };
        float3 valueText  = { 1, .25f, .25f };

        textManager.addText(1, 0, "SVBRDF");

        textManager.addText(0, 2, "Setting");
        textManager.addText(1, 2, "Value");
        textManager.addText(2, 2, "Controls");

        int row = 2;

#define MEMBER_NUMBER(member) [this] { return static_cast<double>(this->member); }
#define MEMBER_STRING(member) [this] (TextManager::TextBuffer &buf) { sprintf_s(buf, "%s", this->member.c_str()); }

        ++row; textManager.addText(0, row, "Move camera");   textManager.addText(2, row, "(WASD)");
        ++row; textManager.addText(0, row, "Turn camera");   textManager.addText(2, row, "(Arrows)");
        ++row; textManager.addText(0, row, "Fast movement"); textManager.addText(2, row, "(Hold Ctrl)");

        ++row;

        ++row; textManager.addText(0, row, "Selected material"); textManager.addCallback(1, row, MEMBER_STRING(state.svbrdfName), valueText); textManager.addText(2, row, "(ZX or 9)");
        ++row; textManager.addText(0, row, "Selected mesh");     textManager.addCallback(1, row, MEMBER_STRING(state.meshName), valueText);   textManager.addText(2, row, "(CV or 0)");

        ++row;

        ++row; textManager.addEnum(0, row, "Mesh mode",      meshMode,          normalText, valueText); textManager.addText(2, row, "(1)");
        ++row; textManager.addEnum(0, row, "Displacement",   displacementMode,  normalText, valueText); textManager.addText(2, row, "(2)");
        ++row; textManager.addEnum(0, row, "Shadows",        shadowMode,        normalText, valueText); textManager.addText(2, row, "(3)");
        ++row; textManager.addEnum(0, row, "Antialiasing",   state.aaMode,      normalText, valueText); textManager.addText(2, row, "(4)");
        ++row; textManager.addEnum(0, row, "Normals",        normalMode,        normalText, valueText); textManager.addText(2, row, "(5)");
        ++row; textManager.addBool(0, row, "Normal mapping", useNormalMapping,  normalText, valueText); textManager.addText(2, row, "(6)");
        ++row; textManager.addEnum(0, row, "Tone mapping",   state.tonemapMode, normalText, valueText); textManager.addText(2, row, "(7)");
        ++row; textManager.addEnum(0, row, "Lighting",       lightingMode,      normalText, valueText); textManager.addText(2, row, "(8)");

        ++row; textManager.addNumberCallback(0, row, "Displacement triangle area", MEMBER_NUMBER(state.displacementDensity),   normalText, valueText); textManager.addText(2, row, "(TG)");
        ++row; textManager.addNumberCallback(0, row, "Displacement magnitude",     MEMBER_NUMBER(state.displacementMagnitude), normalText, valueText); textManager.addText(2, row, "(RF)");

        ++row;

        ++row; textManager.addBool(          0, row, "VR rendering", useOculus, normalText, valueText);                    textManager.addText(2, row, "(Enter)");
        ++row; textManager.addNumberCallback(0, row, "VR scale",     MEMBER_NUMBER(state.vrScale), normalText, valueText); textManager.addText(2, row, "(IK)");
        ++row; textManager.addText(          0, row, "VR recenter");                                                       textManager.addText(2, row, "(Space bar)");

        ++row;

        ++row; textManager.addNumberCallback(0, row, "Amount of lights",    MEMBER_NUMBER(state.lights.size()), normalText, valueText); textManager.addText(2, row, "(Numpad +-)");
        ++row; textManager.addNumberCallback(0, row, "Lights with shadows", MEMBER_NUMBER(state.shadowLights), normalText, valueText);  textManager.addText(2, row, "(Numpad .0)");
        ++row; textManager.addNumber        (0, row, "Selected light", selectedLight, normalText, valueText);                           textManager.addText(2, row, "(Numpad 13)");
        ++row; textManager.addText(0, row, "Move light");               textManager.addText(2, row, "(Numpad 845679)");
        ++row; textManager.addText(0, row, "Adjust light intensity");   textManager.addText(2, row, "(Numpad */)");
        ++row; textManager.addText(0, row, "Adjust ambient intensity"); textManager.addText(2, row, "(PgUp/PgDn)");

        ++row;

        ++row; textManager.addText(0, row, "Load preset");            textManager.addText(2, row, "(F1...F10)");
        ++row; textManager.addText(0, row, "Load preset dialog");     textManager.addText(2, row, "(F11)");
        if (rwPresets)
        {
            ++row; textManager.addText(0, row, "Save preset");            textManager.addText(2, row, "(Ctrl + F1...F10)");
        }
        ++row; textManager.addText(0, row, "Toggle wireframe");       textManager.addText(2, row, "(Del)");
        ++row; textManager.addText(0, row, "Toggle FPS measurement"); textManager.addText(2, row, "(Home)");
        ++row; textManager.addText(0, row, "Toggle help");            textManager.addText(2, row, "(Tab)");

#undef MEMBER_NUMBER
#undef MEMBER_STRING
    }

    void setState(const RenderingState &s)
    {
        state = s;

        state.save(stdout);

        materialIndex = std::max(0, materials.indexOf(state.svbrdfName));
        meshIndex     = meshes.indexOf(state.meshName);

        if (meshIndex < 0)
        {
            meshMode  = MeshMode::SingleQuad;
            meshIndex = 0;
        }
        else
        {
            meshMode = MeshMode::LoadedMesh;
        }

        displacementMode = DisplacementMode::NoDisplacement;
        lightingMode = LightingMode::ForwardLighting;
        shadowMode = ShadowMode::NoShadows;
        shadowPcfTaps = ShadowPcfTaps;
        shadowKernelWidth = ShadowKernelWidth;

        if (meshMode == MeshMode::SingleQuad)
        {
            normalMode = NormalMode::ConstantNormal;
            useNormalMapping = true;
        }
        else 
        {
            normalMode = NormalMode::InterpolatedNormals;
            useNormalMapping = displacementMode == DisplacementMode::NoDisplacement;
        }

        if (meshMode != MeshMode::SingleQuad)
            lightingMode = LightingMode::ForwardLighting;

        lightingPrecision = TextureSpaceLightingPrecision::Float11_11_10;

        lightIntensity   = 1.f;

        camera = FPSCamera(CameraButtons,
                           toVec(state.cameraPosWorld),
                           toRadians(state.cameraYawDegrees),
                           toRadians(state.cameraPitchDegrees));

        update(true);
    }

    void updatePresets()
    {
        std::string preset;

        int presetKeys[] = {
            VK_F1,
            VK_F2,
            VK_F3,
            VK_F4,
            VK_F5,
            VK_F6,
            VK_F7,
            VK_F8,
            VK_F9,
            VK_F10,
        };

        for (size_t i = 0; i < size(presetKeys); ++i)
        {
            if (keyPressed(presetKeys[i]))
                preset = QuickPresetFilenames[i];
        }

        if (!preset.empty())
        {
            if (keyHeld(VK_CONTROL))
            {
                if (rwPresets)
                {
                    updateState();
                    state.save(dataDirectory + "/" + preset);
                }
                else
                {
                    log("Saving presets disabled, unless the --rw-presets switch is used.\n");
                }
            }
            else
            {
                RenderingState s;
                if (s.load(dataDirectory + "/" + preset))
                    setState(s);
            }
        }
        else if (keyPressed(VK_F11))
        {
            RenderingState s;
            if (s.load())
                setState(s);
        }
    }

    void updateState()
    {
        if (state.svbrdfName != activeMaterial.name)
            state.svbrdfName = activeMaterial.name;

        if (activeMesh.valid() && meshMode == MeshMode::LoadedMesh)
        {
            if (state.meshName != activeMesh.name)
                state.meshName = activeMesh.name;
        }
        else
        {
            state.meshName = "";
        }

        state.cameraPosWorld     = toF3(camera.position());
        state.cameraYawDegrees   = toDegrees(camera.yaw());
        state.cameraPitchDegrees = toDegrees(camera.pitch());
    }

    void addNewLight()
    {
        Light l;

        l.positionWorld = state.lights[selectedLight].positionWorld;

        l.colorHDR[1] = 1;
        l.colorHDR[2] = 1;
        l.colorHDR[0] = 1;

        l.falloffMultiplier = 1;

        state.lights.emplace_back(l);
        selectedLight = static_cast<int>(state.lights.size() - 1);
    }

    void removeLight()
    {
        if (state.lights.size() > 1)
        {
            state.lights.erase(state.lights.begin() + selectedLight);
            selectedLight = std::min(selectedLight, static_cast<int>(state.lights.size() - 1));
        }
    }

    void update(bool forceInit = false)
    {
        updatePresets();

        camera.update();

        toggleValue("Show help", VK_TAB, showHelp);

        updateValueClamp('Y', 'H', lightIntensity, 0.05f, 0, 10);

        updateValueMultiply(VK_PRIOR, VK_NEXT, state.ambientHDR[0], 1.1f, 0.f, 1.f);
        updateValueMultiply(VK_PRIOR, VK_NEXT, state.ambientHDR[1], 1.1f, 0.f, 1.f);
        updateValueMultiply(VK_PRIOR, VK_NEXT, state.ambientHDR[2], 1.1f, 0.f, 1.f);

        updateValueWrap(VK_NUMPAD3, VK_NUMPAD1, selectedLight, 1, 0, static_cast<int>(state.lights.size()));
        updateValueClamp(VK_NUMPAD6, VK_NUMPAD4, state.lights[selectedLight].positionWorld[0], LightPosIncrement, -LightPosExtent, LightPosExtent);
        updateValueClamp(VK_NUMPAD9, VK_NUMPAD7, state.lights[selectedLight].positionWorld[1], LightPosIncrement, -LightPosExtent, LightPosExtent);
        updateValueClamp(VK_NUMPAD8, VK_NUMPAD5, state.lights[selectedLight].positionWorld[2], LightPosIncrement, -LightPosExtent, LightPosExtent);
        updateValueMultiply(VK_MULTIPLY, VK_DIVIDE, state.lights[selectedLight].colorHDR[0], 1.1f, 0.f, LightMaxIntensity);
        updateValueMultiply(VK_MULTIPLY, VK_DIVIDE, state.lights[selectedLight].colorHDR[1], 1.1f, 0.f, LightMaxIntensity);
        updateValueMultiply(VK_MULTIPLY, VK_DIVIDE, state.lights[selectedLight].colorHDR[2], 1.1f, 0.f, LightMaxIntensity);

        bool changedLights = false;

        if (keyPressed(VK_ADD))
        {
            addNewLight();
            changedLights = true;
        }
        if (keyPressed(VK_SUBTRACT))
        {
            removeLight();
            changedLights = true;
        }
        changedLights |= updateValueMax(VK_DECIMAL, VK_NUMPAD0, state.shadowLights, static_cast<unsigned>(state.lights.size()));

        // updateValueClamp('4', '3', shadowPcfTaps, 1, 1, 30);
        // updateValueMultiply('6', '5', shadowKernelWidth, 1.1f, 0.f, 1000.f);


        bool changedRenderer = toggleValue("Mesh mode", '1', meshMode);
        changedRenderer     |= toggleValue("Displacement mode", '2', displacementMode);
        changedRenderer     |= toggleValue("Shadows", '3', shadowMode);
        changedRenderer     |= toggleValue("Lighting", '8', lightingMode);
        bool changedAA = toggleValue("Antialiasing", '4', state.aaMode);
        toggleValue("Normals", '5', normalMode);
        toggleValue("Normal mapping", '6', useNormalMapping);
        toggleValue("Tone mapping", '7', state.tonemapMode);
        toggleValue("Wireframe", VK_DELETE, wireframe);

        // changedRenderer     |= toggleValue("Lighting precision", VK_F11, lightingPrecision);

        bool changedVR = toggleValue("VR rendering", VK_RETURN, useOculus);
        updateValueClamp('I', 'K', state.vrScale, 1, -1, 10);

        if (useOculus && !oculus.isConnected())
        {
            log("Oculus Rift not found. VR rendering disabled.\n");
            useOculus = false;
        }

        if (keyPressed(VK_SPACE))
            oculus.recenter();

        if (meshMode == MeshMode::LoadedMesh)
        {
            // Texture space lighting doesn't work with arbitrary meshes
            lightingMode = LightingMode::ForwardLighting;
            if (normalMode == NormalMode::ConstantNormal)
                normalMode = NormalMode::InterpolatedNormals;
        }

        bool loadMaterial        = updateValueWrap('X', 'Z', materialIndex, 1, 0, materials.size()) || forceInit;
        bool loadMesh            = updateValueWrap('V', 'C', meshIndex,     1, 0, meshes.size())    || forceInit;

        bool changedHeight       = updateValueMultiply('R', 'F', state.displacementMagnitude, 1.1f, 0.f,  1.f);
        bool changedTessellation = updateValueMultiply('T', 'G', state.displacementDensity,   2.f,  0.5f, 64.f, true);
        bool initRenderer        = changedRenderer || changedHeight || changedTessellation || loadMaterial || loadMesh || changedLights;

        // initRenderer |= updateValueClamp('E', 'Q', state.shadowDepthBias,   1, -100, 100);
        // initRenderer |= updateValueClamp(VK_NUMPAD8, VK_NUMPAD5, state.shadowSSDepthBias, .05f, -100, 100);

        if (loadMaterial)
        {
            activeMaterial = materials.load(materialIndex);
            changedTessellation = true;
        }

        if (!activeMaterial.valid() || keyPressed('9'))
        {
            if (!activeMaterial.valid())
                log("No valid material to render with. Please select a material.\n");

            materials.loadDialog(activeMaterial);
            changedTessellation = true;
            initRenderer = true;
        }

        check(activeMaterial.valid(), "Must have a valid material to render with.\n");

        if (loadMesh)
        {
            activeMesh = meshes.load(meshIndex, computeTargetTriangleArea(activeMaterial, state.displacementDensity));
        }
        else if (keyPressed('0'))
        {
            meshes.loadDialog(activeMesh, computeTargetTriangleArea(activeMaterial, state.displacementDensity));
            meshMode = MeshMode::LoadedMesh;
            initRenderer = true;
        }

        if (activeMesh.valid() && meshMode == MeshMode::LoadedMesh && changedTessellation)
        {
            MeshCollection::retessellate(activeMesh, computeTargetTriangleArea(activeMaterial, state.displacementDensity));
            initRenderer = true;
        }
        else if (!activeMesh.valid() && meshMode == MeshMode::LoadedMesh)
        {
            log("No valid mesh for .OBJ mesh rendering. Switching to single quad.\n");
            meshMode = MeshMode::SingleQuad;
            initRenderer = true;
        }

        if (displacementMode != DisplacementMode::NoDisplacement)
        {
            if (!activeMaterial.heightMap.valid()
                || activeMaterial.heightMapCPU.width <= 0
                || activeMaterial.heightMapCPU.height <= 0)
            {
                log("No valid height map for material \"%s\", displacement mapping disabled.\n",
                    activeMaterial.name.c_str());
                displacementMode = DisplacementMode::NoDisplacement;
            }
        }

        if (initRenderer)
        {
            renderer = std::make_shared<SVBRDFRenderer>(
                meshMode, displacementMode,
                lightingMode, lightingPrecision);
            renderer->init(activeMaterial, &activeMesh, computeConstants());
        }

        if (forceInit || changedVR || changedAA)
            initAA();

        renderer->updateLights(state.lights);

        updateState();
    }

    void initAA()
    {
        auto rtDesc = texture2DDesc(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

        auto zDesc = texture2DDesc(1, 1, DXGI_FORMAT_D32_FLOAT);
        zDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        rtDesc.SampleDesc.Quality = 0;
        zDesc.SampleDesc.Quality = 0;

        unsigned supersampling = 1;
        unsigned mipLevels     = 1;

        switch (state.aaMode)
        {
        case AntialiasingMode::NoAA:
        default:
            return;
        case AntialiasingMode::SSAA2x:
            supersampling = 2;
            mipLevels     = 2;
            rtDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            rtDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
            break;
        case AntialiasingMode::SSAA4x:
            supersampling = 4;
            mipLevels     = 3;
            rtDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            rtDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
            break;
        case AntialiasingMode::MSAA4x:
            rtDesc.SampleDesc.Count = 4;
            zDesc.SampleDesc.Count = 4;
            break;
        }

        auto makeTargets = [&](Resource &rt, Resource &z, unsigned w, unsigned h)
        {
            w *= supersampling;
            h *= supersampling;

            rtDesc.Width     = w;
            rtDesc.Height    = h;
            rtDesc.MipLevels = mipLevels;
            zDesc.Width      = w;
            zDesc.Height     = h;

            rt = Resource(rtDesc);
            z  = Resource(zDesc);
        };

        if (renderToOculus())
        {
            for (unsigned eye = 0; eye < 2; ++eye)
            {
                makeTargets(aaTargets[eye], aaDepth[eye],
                            oculus.eyes[eye].size.w,
                            oculus.eyes[eye].size.h);
            }
        }
        else
        {
            makeTargets(aaTargets[0], aaDepth[0],
                        oculus.mirrorW,
                        oculus.mirrorH);
        }
    }

    SVBRDFRenderer::Constants computeConstants(const XMMATRIX *viewProjection = nullptr,
                                                const XMVECTOR *cameraPosition = nullptr)
    {
        SVBRDFRenderer::Constants constants;
        zero(constants);

        if (viewProjection)
            constants.viewProj = *viewProjection;

        if (cameraPosition)
            constants.cameraPosition = *cameraPosition;

        constants.ambientLight       = toVec(state.ambientHDR);
        constants.tonemapMode        = state.tonemapMode;
        constants.maxLuminance       = 2.f;

        constants.shadowLights       = shadowMode == ShadowMode::NoShadows ? 0 : state.shadowLights;
        constants.shadowResolution   = state.shadowResolution;
        constants.shadowDepthBias    = state.shadowDepthBias;
        constants.shadowSSDepthBias  = state.shadowSSDepthBias;

        constants.shadowPcfTaps      = std::max(1, shadowPcfTaps);
        constants.shadowKernelWidth  = shadowKernelWidth;

        constants.normalMode         = static_cast<uint>(normalMode);

        // Disable normal mapping for the displacement mapped mesh unless
        // constant normals are used. The material normals are already included
        // in the geometry, and normal mapping in this situation would account
        // for them twice. 
        bool disableNormalMap =
            displacementMode == DisplacementMode::CPUDisplacementMapping &&
            normalMode != NormalMode::ConstantNormal;

        constants.useNormalMapping      = static_cast<uint>(useNormalMapping && !disableNormalMap);
        constants.displacementDensity   = state.displacementDensity;
        constants.displacementMagnitude = state.displacementMagnitude;
        constants.tessellation = displacementMode == DisplacementMode::GPUDisplacementMapping;

        constants.wireframe = wireframe;

        return constants;
    }

    void renderView(
        Resource &renderTarget, Resource &depthBuffer,
        const XMMATRIX &viewProjection, XMVECTOR cameraPosition)
    {
        auto constants = computeConstants(&viewProjection, &cameraPosition);

        {
            GPUScope clears(L"Clear render targets");
            float black[] = { 0, 0, 0, 1 };
            context->ClearRenderTargetView(renderTarget.rtv, black);
            // Clear to min depth since we are using inverse Z
            context->ClearDepthStencilView(depthBuffer.dsv, D3D11_CLEAR_DEPTH, D3D11_MIN_DEPTH, 0);
        }

        {
            GPUScope scope(L"Render SVBRDF");
            renderer->render(cb, activeMaterial, constants, renderTarget, depthBuffer);
        }

        {
            GPUScope scope(L"Render light indicators");
            setRenderTarget(renderTarget, &depthBuffer);
            for (size_t i = 0; i < state.lights.size(); ++i)
            {
                float size = (i == selectedLight)
                    ? 0.15f
                    : 0.05f;
                auto &L = state.lights[i];
                lightIndicator.render(cb, size, toVec(L.positionWorld), constants.viewProj,
                                      L.colorHDR[0], L.colorHDR[1], L.colorHDR[2]);
            }
            setRenderTarget(nullptr);
        }
    }

    bool renderToOculus() const
    {
        return useOculus && oculus.isActive();
    }

    bool canVsync() const
    {
        return !renderToOculus();
    }

    static XMMATRIX viewMatrix(XMVECTOR cameraPosition, XMVECTOR cameraRotation)
    {
        XMVECTOR forwardRH = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), cameraRotation);

        XMVECTOR target    = XMVectorAdd(cameraPosition, forwardRH);
        XMVECTOR up        = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), cameraRotation);

        XMMATRIX view = XMMatrixLookAtRH(cameraPosition, target, up);

        return view;
    }

    void renderViewWithAA(
        Resource &finalRT, Resource &finalZ,
        const XMMATRIX &viewProjection, XMVECTOR cameraPosition,
        Resource &aaRT, Resource &aaZ)
    {
        switch (state.aaMode)
        {
        case AntialiasingMode::NoAA:
        {
            renderView(finalRT, finalZ, viewProjection, cameraPosition);
            break;
        }
        case AntialiasingMode::MSAA4x:
        {
            renderView(aaRT, aaZ, viewProjection, cameraPosition);
            // FIXME: Fixed function resolve might not be sRGB correct. :(
            context->ResolveSubresource(finalRT.texture, 0, aaRT.texture, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
            break;
        }
        case AntialiasingMode::SSAA2x:
        case AntialiasingMode::SSAA4x:
        {
            renderView(aaRT, aaZ, viewProjection, cameraPosition);
            // FIXME: This might not be sRGB correct
            context->GenerateMips(aaRT.srv);
            unsigned mips     = aaRT.textureDescriptor().MipLevels;
            context->CopySubresourceRegion(finalRT.texture, 0, 0, 0, 0,
                                           aaRT.texture, D3D11CalcSubresource(mips - 1, 0, mips), nullptr);
            break;
        }
        default:
            check(false, "Unknown antialiasing mode!");
            break;
        }

        if (showHelp)
        {
            GPUScope scope(L"Render text");
            textManager.updateTextCache();

            uint2 textCoords;
            if (useOculus)
                textCoords = { 400, 300 };
            else
                textCoords = { 10, 10 };

            textManager.render(finalRT, textCoords);
        }

#if defined(DEBUG_SHADOW_TEXEL_UNPROJECT)
        if (shadowMode == ShadowMode::ShadowMapping)
        {
            static int shadowSlice = 5;
            updateValueWrap('I', 'K', shadowSlice, 1, 0, 6);
            setRenderTarget(finalRT);
            renderer->unprojectShadowMap(cb, computeConstants(&viewProjection), shadowSlice);
            setRenderTarget(nullptr);
        }
#endif
    }

    void render(Resource &renderTarget, Resource &depthBuffer)
    {
        renderer->renderViewportIndependent(cb, activeMaterial, computeConstants());

        if (renderToOculus())
        {
            // Sample sensors as close as possible to rendering, so right before.
            oculus.samplePose();

            // First, render both eyes
            for (auto &&eye : oculus.eyes)
            {
                GPUScope scope(L"Render eye");

                // Apply head pose on top of the camera pose
                XMVECTOR eyeRot = XMVectorSet(
                    eye.pose.Orientation.x,
                    eye.pose.Orientation.y,
                    eye.pose.Orientation.z,
                    eye.pose.Orientation.w);

                XMVECTOR eyePos = XMVectorSet(
                    eye.pose.Position.x,
                    eye.pose.Position.y,
                    eye.pose.Position.z,
                    0);

                XMVECTOR basePosition = camera.position();
                XMVECTOR baseRotation = camera.rotation();

                XMVECTOR eyeOffset    = XMVector3Rotate(eyePos, baseRotation);

                XMVECTOR cameraPosition;
                XMVECTOR cameraRotation;

                float headPositionMultiplier;

                if (state.vrScale < 0)
                    headPositionMultiplier = 0;
                else
                    headPositionMultiplier = std::pow(10.f, state.vrScale / 4.f);

                eyeOffset = XMVectorMultiply(eyeOffset, XMVectorReplicate(headPositionMultiplier));

                cameraPosition = XMVectorAdd(basePosition, eyeOffset);
                cameraRotation = XMQuaternionMultiply(eyeRot, baseRotation);

                XMMATRIX view = viewMatrix(cameraPosition, cameraRotation);

                ovrMatrix4f ovrProj    = ovrMatrix4f_Projection(eye.fov, NearZ, FarZ,
                                                                ovrProjection_FarLessThanNear |
                                                                ovrProjection_RightHanded);

			    XMMATRIX proj = XMMatrixSet(ovrProj.M[0][0], ovrProj.M[1][0], ovrProj.M[2][0], ovrProj.M[3][0],
				                            ovrProj.M[0][1], ovrProj.M[1][1], ovrProj.M[2][1], ovrProj.M[3][1],
				                            ovrProj.M[0][2], ovrProj.M[1][2], ovrProj.M[2][2], ovrProj.M[3][2],
				                            ovrProj.M[0][3], ovrProj.M[1][3], ovrProj.M[2][3], ovrProj.M[3][3]);

                XMMATRIX viewProjection = XMMatrixMultiply(view, proj);

                eye.next();

                renderViewWithAA(eye.active(), eye.depthBuffer, viewProjection, cameraPosition,
                                 aaTargets[eye.number], aaDepth[eye.number]);
            }

            // Submit the eyes to OVR
            ovrLayerEyeFov frame = oculus.frame();
            auto layers = &frame.Header;
            oculus.assertStatus(ovr_SubmitFrame(oculus.session, 0, nullptr, &layers, 1));

            // Then, copy the mirror texture contents to the target of the rendering
            context->CopyResource(renderTarget.texture, oculus.mirrorD3DTexture());
        }
        else
        {
            // No headset active, render normally to the window
            unsigned width  = oculus.mirrorW;
            unsigned height = oculus.mirrorH;

            XMVECTOR cameraPosition = camera.position();

            XMMATRIX view = viewMatrix(cameraPosition, camera.rotation());

            XMMATRIX proj = projection(width, height, NearZ, FarZ, DefaultVerticalFOV, DepthMode::InverseDepth);

            XMMATRIX viewProjection = XMMatrixMultiply(view, proj);

            renderViewWithAA(renderTarget, depthBuffer, viewProjection, cameraPosition,
                             aaTargets[0], aaDepth[0]);
        }
    }
};

struct Args
{
    const char *dataDirectory;
    unsigned width;
    unsigned height;
    bool readWritePresets;

    Args()
        : dataDirectory(nullptr)
        , width(DefaultWindowWidth)
        , height(DefaultWindowHeight)
        , readWritePresets(false)
    {}
};

Args processArgs(int argc, const char *argv[])
{
    auto it  = argv + 1;
    auto end = argv + argc;

    Args args;

    while (it < end)
    {
        std::string a(*it);
        if (a == "--data")
        {
            ++it;
            args.dataDirectory = *it;
        }
        else if (a == "--width")
        {
            ++it;
            args.width = atoi(*it);
        }
        else if (a == "--height")
        {
            ++it;
            args.height = atoi(*it);
        }
        else if (a == "--rw-presets")
        {
            args.readWritePresets = true;
        }
        else
        {
            log("Usage: %s [--help] [--data DATA_DIRECTORY] [--width WIDTH] [--height HEIGHT]\n", argv[0]);
            log("   --help                 Print these usage instructions.\n");
            log("   --width WIDTH          Set the width of the created window (default: %u)\n", DefaultWindowWidth);
            log("   --height HEIGHT        Set the height of the created window (default: %u)\n", DefaultWindowHeight);
            log("   --data DATA_DIRECTORY  Use DATA_DIRECTORY as the data directory.\n");
            log("   --rw-presets           Allow saving presets with Ctrl + F1...F10\n");
            exit(0);
        }

        ++it;
    }

    return args;
}

int main(int argc, const char *argv[])
{
    Args args = processArgs(argc, argv);

    Oculus oculus(args.width, args.height);

    unsigned windowW = oculus.mirrorW;
    unsigned windowH = oculus.mirrorH;

    log("Using %u x %u resolution.\n", windowW, windowH);

    Window window("SVBRDF", windowW, windowH);
    keyboardWindow(window.hWnd);
    Graphics graphics(window.hWnd, windowW, windowH, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    graphics.maximumLatency(1);

    oculus.createOutputTextures();
    SVBRDFOculus svbrdfOculus(oculus, args.dataDirectory ? args.dataDirectory : "", args.readWritePresets);

    Resource depthBuffer;
    {
        D3D11_TEXTURE2D_DESC zDesc = texture2DDesc(
            graphics.swapChain.width,
            graphics.swapChain.height,
            DXGI_FORMAT_D32_FLOAT);
        zDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthBuffer = Resource(zDesc);
        RESOURCE_DEBUG_NAME(depthBuffer);
    }

    static const size_t NumFrameTimeSamples = 30;
    bool measureFrameTime = false;
    Timer frameTimer;
    std::vector<double> frameTimeSamples;
    frameTimeSamples.reserve(NumFrameTimeSamples);

    window.run([&](Window &)
    {
        GPUScope frame(L"Frame");

        svbrdfOculus.update();
        {
            svbrdfOculus.render(graphics.swapChain.backBuffer, depthBuffer);
        }

        frame.end();

        toggleValue("Frame time measurement", VK_HOME, measureFrameTime);
        bool vsync = svbrdfOculus.canVsync() && !measureFrameTime;
        graphics.present(vsync);

        double t = frameTimer.seconds();
        frameTimer = Timer();

        if (measureFrameTime)
        {
            frameTimeSamples.emplace_back(t);
            if (frameTimeSamples.size() >= NumFrameTimeSamples)
            {
                double sum = std::accumulate(frameTimeSamples.begin(), frameTimeSamples.end(), 0.);
                double avgTime = sum / static_cast<double>(frameTimeSamples.size());
                frameTimeSamples.clear();
                log("Average frame time: %10.2f ms\n", avgTime * 1000.0);
            }
        }
        else
        {
            frameTimeSamples.clear();
        }

        return !keyPressed(VK_ESCAPE);
    });

    return 0;
}

