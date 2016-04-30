#pragma once

#include "Utils.hpp"

#include <d3d11.h>
#include <d3d11_1.h>
#include <DirectXMath.h>

#include "OVR_CAPI_D3D.h"

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <numeric>
#include <initializer_list>

extern CComPtr<ID3D11Device> device;
extern CComQIPtr<ID3DUserDefinedAnnotation> annotation;
extern CComPtr<ID3D11DeviceContext> context;

using DirectX::XM_PI;

struct Resource
{
    DXGI_FORMAT format;
    unsigned stride;
    CComPtr<ID3D11Texture2D> texture;
    CComPtr<ID3D11Buffer> buffer;

    CComPtr<ID3D11ShaderResourceView> srv;
    CComPtr<ID3D11UnorderedAccessView> uav;
    CComPtr<ID3D11RenderTargetView> rtv;
    CComPtr<ID3D11DepthStencilView> dsv;

    Resource() : format(DXGI_FORMAT_UNKNOWN) {}
    Resource(CComPtr<ID3D11Resource> resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);
    Resource(CComPtr<ID3D11Buffer> buffer, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);
    Resource(CComPtr<ID3D11Texture2D> texture);
    Resource(const D3D11_BUFFER_DESC &bufferDesc, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, const void *initialData = nullptr, size_t initialBytes = 0);
    Resource(const D3D11_TEXTURE2D_DESC &textureDesc, const D3D11_SUBRESOURCE_DATA *initialData = nullptr);

    bool valid() const;
    explicit operator bool() const { return valid(); }

    ID3D11Resource &resource();

    void views(
        const D3D11_SHADER_RESOURCE_VIEW_DESC *srvDesc = nullptr,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC *uavDesc = nullptr,
        const D3D11_RENDER_TARGET_VIEW_DESC *rtvDesc = nullptr,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *dsvDesc = nullptr);

    D3D11_RESOURCE_DIMENSION dimension();
    D3D11_BUFFER_DESC bufferDescriptor();
    D3D11_TEXTURE2D_DESC textureDescriptor();

    void name(const char *fmt, ...);
};

#define RESOURCE_DEBUG_NAME(res) (res).name("%s", #res)

struct SwapChain
{
    int width;
    int height;
    CComPtr<IDXGISwapChain> swapChain;
    Resource backBuffer;
};

struct Graphics
{
    SwapChain swapChain;

    Graphics(HWND hWnd, int width, int height, DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
    ~Graphics();

    void maximumLatency(unsigned frames);
    void present(bool vSync = true);
};

template <typename T>
class Binding
{
    std::array<T *, 1> storage;
public:
    Binding(T *t = nullptr)
    {
        storage[0] = t;
    }

    operator T * const *()
    {
        return storage.data();
    }
};

template <typename T>
Binding<T> bind(CComPtr<T> &t)
{
    return Binding<T>(t);
}

class ConstantBuffers
{
public:
    typedef std::shared_ptr<CComPtr<ID3D11Buffer>> CB;
private:

    std::unordered_map<size_t, std::vector<CB>> sizeClasses;

    CB get(size_t size);
public:

    template <typename T>
    CB write(const T &t)
    {
        auto cb = get(sizeof(t));
        D3D11_MAPPED_SUBRESOURCE mapped;
        context->Map(*cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &t, sizeof(t));
        context->Unmap(*cb, 0);
        return cb;
    }
};

inline Binding<ID3D11Buffer> bind(ConstantBuffers::CB &cb)
{
    return Binding<ID3D11Buffer>(*cb);
}

struct CS {
    typedef ID3D11ComputeShader type;

    template <typename Bytecode>
    static CComPtr<type> load(const Bytecode &bytecode)
    {
        CComPtr<type> shader;
        checkHR(device->CreateComputeShader(dataPtr(bytecode), sizeBytes(bytecode), nullptr, &shader));
        return shader;
    }
};

struct VS {
    typedef ID3D11VertexShader type;

    template <typename Bytecode>
    static CComPtr<type> load(const Bytecode &bytecode)
    {
        CComPtr<type> shader;
        checkHR(device->CreateVertexShader(dataPtr(bytecode), sizeBytes(bytecode), nullptr, &shader));
        return shader;
    }
};

struct HS {
    typedef ID3D11HullShader type;

    template <typename Bytecode>
    static CComPtr<type> load(const Bytecode &bytecode)
    {
        CComPtr<type> shader;
        checkHR(device->CreateHullShader(dataPtr(bytecode), sizeBytes(bytecode), nullptr, &shader));
        return shader;
    }
};

struct DS {
    typedef ID3D11DomainShader type;

    template <typename Bytecode>
    static CComPtr<type> load(const Bytecode &bytecode)
    {
        CComPtr<type> shader;
        checkHR(device->CreateDomainShader(dataPtr(bytecode), sizeBytes(bytecode), nullptr, &shader));
        return shader;
    }
};

struct PS {
    typedef ID3D11PixelShader type;

    template <typename Bytecode>
    static CComPtr<type> load(const Bytecode &bytecode)
    {
        CComPtr<type> shader;
        checkHR(device->CreatePixelShader(dataPtr(bytecode), sizeBytes(bytecode), nullptr, &shader));
        return shader;
    }
};

template <typename ShaderType>
struct Shader
{
    typedef typename ShaderType::type S;
    CComPtr<S> shader;

    Shader() {}
    Shader(std::nullptr_t) {}

    template <typename Bytecode>
    Shader(const Bytecode &bytecode)
        : shader(ShaderType::load(bytecode))
    {}

    explicit operator bool() const
    {
        return !!shader;
    }
};

struct GraphicsPipeline
{
    Shader<VS> vs;
    Shader<HS> hs;
    Shader<DS> ds;
    Shader<PS> ps;
    Shader<PS> psWireframe;

    D3D11_PRIMITIVE_TOPOLOGY         primitiveTopology;
    CComPtr<ID3D11DepthStencilState> depthStencilState;
    CComPtr<ID3D11DepthStencilState> depthStencilStateWireframe;
    CComPtr<ID3D11RasterizerState>   rasterizerState;
    CComPtr<ID3D11RasterizerState>   rasterizerStateWireframe;
    CComPtr<ID3D11BlendState>        blendState;

    CComPtr<ID3D11InputLayout>       inputLayout;

    void initStates(D3D11_PRIMITIVE_TOPOLOGY topology,
                    const D3D11_DEPTH_STENCIL_DESC *dss = nullptr,
                    const D3D11_RASTERIZER_DESC    *rss = nullptr,
                    const D3D11_BLEND_DESC         *bs  = nullptr);

    GraphicsPipeline()
        : primitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
    {}

    template <typename VSBytecode, typename PSBytecode>
    GraphicsPipeline(const VSBytecode &vsBytecode,
                     const PSBytecode &psBytecode,
                     D3D11_PRIMITIVE_TOPOLOGY topology,
                     const D3D11_DEPTH_STENCIL_DESC *dss = nullptr,
                     const D3D11_RASTERIZER_DESC    *rss = nullptr,
                     const D3D11_BLEND_DESC         *bs  = nullptr)
        : vs(vsBytecode)
        , ps(psBytecode)
    {
        initStates(topology, dss, rss, bs);
    }

    template <typename VSBytecode, typename HSBytecode, typename DSBytecode, typename PSBytecode>
    GraphicsPipeline(const VSBytecode &vsBytecode,
                     const HSBytecode &hsBytecode,
                     const DSBytecode &dsBytecode,
                     const PSBytecode &psBytecode,
                     D3D11_PRIMITIVE_TOPOLOGY topology,
                     const D3D11_DEPTH_STENCIL_DESC *dss = nullptr,
                     const D3D11_RASTERIZER_DESC    *rss = nullptr,
                     const D3D11_BLEND_DESC         *bs  = nullptr)
        : vs(vsBytecode)
        , hs(hsBytecode)
        , ds(dsBytecode)
        , ps(psBytecode)
    {
        initStates(topology, dss, rss, bs);
    }

    void bind();
    void bindWireframe();
};

Resource downloadForDebugging(Resource &buffer);

template <typename T>
std::vector<T> downloadForDebugging(Resource &buffer)
{
    // This function is very slow since it causes a GPU-CPU sync point,
    // but for debugging it's convenient.
    Resource downloaded = downloadForDebugging(buffer);

    size_t bytes = downloaded.bufferDescriptor().ByteWidth;
    size_t elements = bytes / sizeof(T);
    std::vector<T> v(elements);

    D3D11_MAPPED_SUBRESOURCE mapped;
    zero(mapped);
    context->Map(downloaded.buffer, 0, D3D11_MAP_READ, 0, &mapped);
    memcpy(v.data(), mapped.pData, bytes);
    context->Unmap(downloaded.buffer, 0);

    return v;
}

struct FloatPixelBuffer
{
    int width;
    int height;
    int channels;
    std::vector<float> pixels;

    FloatPixelBuffer() : width(-1), height(-1), channels(-1) {}
    FloatPixelBuffer(int width, int height, int channels);

    DXGI_FORMAT format() const;
    size_t bytes() const;
    float *operator()(int x, int y);
    float &operator()(int x, int y, int ch);
};

class GPUScope
{
    ID3DUserDefinedAnnotation *m_annotation;
public:
    GPUScope(const wchar_t *name);
    ~GPUScope();

    void end();

    GPUScope(const GPUScope &) = delete;
    GPUScope(GPUScope &&) = delete;
    GPUScope &operator=(const GPUScope &) = delete;
    GPUScope &operator=(GPUScope &&) = delete;
};
void gpuMarker(const wchar_t *fmt, ...);

Resource loadImage(const char *filename, size_t *loadedBytes = nullptr);
Resource loadPFMImage(const char *filename, FloatPixelBuffer *pixels = nullptr);

void setRenderTarget(ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv = nullptr);
inline void setRenderTarget(Resource &renderTarget, Resource *depthBuffer = nullptr)
{
    setRenderTarget(renderTarget.rtv, depthBuffer ? depthBuffer->dsv : nullptr);
}
inline void setDepthOnly(Resource &depthBuffer)
{
    setRenderTarget(nullptr, depthBuffer.dsv);
}

void setVertexBuffers(Resource *vertexBuffer, Resource *indexBuffer);

enum class DepthMode
{
    ForwardDepth,
    InverseDepth,
    Always,
};
static const float DefaultVerticalFOV = XM_PI / 3.f; // 60 degrees vertical field of view

DirectX::XMMATRIX projection(unsigned width, unsigned height, float nearZ, float farZ, float verticalFOV = DefaultVerticalFOV, DepthMode depthMode = DepthMode::ForwardDepth);

D3D11_TEXTURE2D_DESC texture2DDesc(unsigned width, unsigned height, DXGI_FORMAT format);
D3D11_DEPTH_STENCIL_DESC depthStencilDesc(DepthMode depthMode, bool writeDepth, bool depthTest = true);

template <typename ResourcePtr>
void unbindResources(void (__stdcall ID3D11DeviceContext::*bindFunction)(UINT slot, UINT count, ResourcePtr *), std::initializer_list<UINT> slots)
{
    ResourcePtr none[1] = { nullptr };

    for (UINT slot : slots)
    {
        (context->*bindFunction)(slot, 1, none);
    }
}

struct Vertex
{
    float3 pos;
    float3 normal;
    float2 uv;
    float tessellation;

    // Don't compare normals or tessellation, as those are procedurally generated upon load
    bool operator==(const Vertex &v) const
    {
        return pos == v.pos && uv == v.uv;
    }

    static std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc();
};

void computeVertexNormals(std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);

struct Mesh
{
    std::string name;
    std::vector<std::string> objFiles;
    unsigned vertexAmount;
    unsigned indexAmount;
    DXGI_FORMAT indexFormat;
    std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDesc;

    Resource vertexBuffer;
    Resource indexBuffer;
    float scale;

    bool valid() const
    {
        return vertexBuffer.buffer && indexBuffer.buffer;
    }
};

enum class MeshLoadMode
{
    Normal, // load the mesh as-is
    SwapYZ, // swap the Y and Z axes
};
Mesh loadMesh(const std::vector<std::string> &objFilenames,
              MeshLoadMode loadMode = MeshLoadMode::Normal,
              float tessellationTriangleArea = 0);

inline Mesh loadMesh(std::string objFilename)
{
    std::vector<std::string> files { objFilename };
    return loadMesh(files);
}

CComPtr<ID3D11SamplerState> samplerPoint(D3D11_TEXTURE_ADDRESS_MODE mode = D3D11_TEXTURE_ADDRESS_CLAMP);
CComPtr<ID3D11SamplerState> samplerBilinear(D3D11_TEXTURE_ADDRESS_MODE mode = D3D11_TEXTURE_ADDRESS_CLAMP);
CComPtr<ID3D11SamplerState> samplerAnisotropic(unsigned maxAnisotropy, D3D11_TEXTURE_ADDRESS_MODE mode = D3D11_TEXTURE_ADDRESS_CLAMP);

enum class CubeMapFace : UINT
{
    XPositive,
    XNegative,
    YPositive,
    YNegative,
    ZPositive,
    ZNegative,
};

DirectX::XMMATRIX cubeMapFaceViewRH(CubeMapFace face, DirectX::XMVECTOR eyePosition);
DirectX::XMMATRIX cubeMapFaceProjRH(float nearZ, float farZ, DepthMode depthMode = DepthMode::ForwardDepth);

struct Oculus
{
    static const unsigned DefaultWidth  = 1280;
    static const unsigned DefaultHeight = 720;

    ovrSession session;
    ovrGraphicsLuid luid;
    ovrHmdDesc hmd;
    unsigned mirrorW;
    unsigned mirrorH;

    struct Eye
    {
        unsigned number;
        ovrSession session;
        ovrSizei size;
        ovrFovPort fov;
        ovrSwapTextureSet *swapTextureSet;
        std::vector<Resource> swapTargets;
        Resource depthBuffer;
        ovrEyeRenderDesc renderDesc;
        ovrPosef pose;

        Eye() {}
        ~Eye();

        Resource &active();
        void next();
    };

    std::vector<Eye> eyes;
    ovrTexture *mirrorTexture;
    double sensorSampleTime;

    Oculus(unsigned width = DefaultWidth, unsigned height = DefaultHeight);
    ~Oculus();

    static void assertStatus(ovrResult result);

    Oculus(const Oculus &) = delete;
    Oculus &operator=(const Oculus &) = delete;
    Oculus(Oculus &&) = delete;
    Oculus &operator=(Oculus &&) = delete;

    void createOutputTextures(
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    ID3D11Texture2D *mirrorD3DTexture();

    bool isConnected() const;
    bool isActive() const;

    void samplePose();
    void recenter();
    ovrLayerEyeFov frame();
};
