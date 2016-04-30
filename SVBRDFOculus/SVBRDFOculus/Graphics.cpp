#include "Graphics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <vector>
#include <fstream>
#include <unordered_set>

#include <objbase.h>
#include <wincodec.h>

using namespace DirectX;

CComPtr<ID3D11Device> device;
CComQIPtr<ID3DUserDefinedAnnotation> annotation;
CComPtr<ID3D11DeviceContext> context;

Graphics::Graphics(HWND hWnd, int width, int height, DXGI_FORMAT swapChainFormat)
{
    checkHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    UINT flags = 0;
#if defined(_DEBUG)
    // Enable DX debug layer in debug mode
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel[1] = { D3D_FEATURE_LEVEL_11_0 };

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    // Use zero defaults for everything
    zero(swapChainDesc);

    swapChainDesc.BufferDesc.Width  = width;
    swapChainDesc.BufferDesc.Height = height;

    // 60 Hz
    swapChainDesc.BufferDesc.RefreshRate.Numerator   = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Format = swapChainFormat;

    // No MSAA
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT
                              // | DXGI_USAGE_UNORDERED_ACCESS
                              | DXGI_USAGE_SHADER_INPUT;

    // Triple buffering for maximum performance
    swapChainDesc.BufferCount = 3;

    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.Windowed     = TRUE;
    swapChainDesc.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;

    D3D_DRIVER_TYPE driverType =
#if 0
        D3D_DRIVER_TYPE_WARP
#else
        D3D_DRIVER_TYPE_HARDWARE
#endif
        ;

    checkHR(D3D11CreateDeviceAndSwapChain(
        nullptr,
        driverType,
        nullptr,
        flags,
        featureLevel, 1,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        &swapChain.swapChain,
        &device,
        nullptr,
        &context));

#if defined(_DEBUG)
    // Catch all debug layer warnings ASAP.
    CComQIPtr<ID3D11InfoQueue> info;
    info = device;
    if (info)
    {
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR,      TRUE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING,    TRUE);
    }

#endif

    CComPtr<ID3D11Texture2D> backBuffer;
    checkHR(swapChain.swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void **>(&backBuffer)));

    swapChain.backBuffer = Resource(std::move(backBuffer));
    swapChain.width  = width;
    swapChain.height = height;
    swapChain.backBuffer.name("Swap chain backbuffer");

    annotation = context;
}

Graphics::~Graphics()
{
    swapChain = SwapChain();
    context = nullptr;
    device  = nullptr;

    CoUninitialize();
}

void Graphics::maximumLatency(unsigned frames)
{
    CComQIPtr<IDXGIDevice1> dxgiDevice;
    dxgiDevice = device;

    check(!!dxgiDevice, "Cannot get interface for SetMaximumFrameLatency.");

    dxgiDevice->SetMaximumFrameLatency(frames);
}

void Graphics::present(bool vSync)
{
    swapChain.swapChain->Present(vSync ? 1 : 0, 0);
}

static unsigned elementSize(const D3D11_BUFFER_DESC &desc, DXGI_FORMAT format)
{
    if (desc.StructureByteStride)
    {
        return desc.StructureByteStride;
    }
    else
    {
        switch (format)
        {
        case DXGI_FORMAT_R32_SINT:
            return sizeof(int32_t);
        case DXGI_FORMAT_R32_UINT:
            return sizeof(uint32_t);
        case DXGI_FORMAT_R32_FLOAT:
            return sizeof(float);
        case DXGI_FORMAT_R32G32_FLOAT:
            return 2 * sizeof(float);
        default:
            check(false, "Unknown format");
            return 0;
        }
    }
}

Resource::Resource(CComPtr<ID3D11Buffer> buffer, DXGI_FORMAT format)
{
    this->buffer = std::move(buffer);
    this->format = format;
    this->stride = elementSize(bufferDescriptor(), format);
    views();
}

Resource::Resource(CComPtr<ID3D11Texture2D> texture)
{
    this->texture = std::move(texture);
    this->format = textureDescriptor().Format;
    this->stride = 0;
    views();
}

Resource::Resource(CComPtr<ID3D11Resource> resource, DXGI_FORMAT format)
{
    D3D11_RESOURCE_DIMENSION dim;
    resource->GetType(&dim);

    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        buffer = resource;
        this->format = format;
        this->stride = elementSize(bufferDescriptor(), format);
    }
    else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        texture = resource;
        this->format = textureDescriptor().Format;
        this->stride = 0;
    }
    else
    {
        check(false, "Unsupported resource kind");
    }

    views();
}

Resource::Resource(const D3D11_BUFFER_DESC &bufferDesc, DXGI_FORMAT format, const void *initialData, size_t initialBytes)
{
    check(initialBytes <= std::numeric_limits<UINT>::max(), "Too many initial bytes.");

    D3D11_SUBRESOURCE_DATA initial;
    zero(initial);
    initial.pSysMem          = initialData;
    initial.SysMemPitch      = static_cast<UINT>(initialBytes);
    initial.SysMemSlicePitch = static_cast<UINT>(initialBytes);

    checkHR(device->CreateBuffer(
        &bufferDesc,
        (initialData && initialBytes) ? &initial : nullptr,
        &buffer));

    this->format = format;
    this->stride = elementSize(bufferDesc, format);
    views();
}

static D3D11_TEXTURE2D_DESC fixupTextureDescriptor(D3D11_TEXTURE2D_DESC textureDesc)
{
    if ((textureDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL) &&
        ((textureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ||
         (textureDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)))
    {
        // Depth targets have to be created with typeless formats
        // in order to use them in shaders.
        switch (textureDesc.Format)
        {
        case DXGI_FORMAT_D32_FLOAT:
            textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            break;
        default:
            break;
        }
    }

    return textureDesc;
}

Resource::Resource(const D3D11_TEXTURE2D_DESC &textureDesc, const D3D11_SUBRESOURCE_DATA *initialData)
{
    checkHR(device->CreateTexture2D(
        &fixupTextureDescriptor(textureDesc),
        initialData,
        &texture));
    this->format = textureDesc.Format;
    this->stride = 0;
    views();
}

bool Resource::valid() const
{
    return buffer != nullptr || texture != nullptr;
}

ID3D11Resource &Resource::resource()
{
    if (buffer)
        return *buffer;
    else
        return *texture;
}

static unsigned elementCount(const D3D11_BUFFER_DESC &desc, DXGI_FORMAT format)
{
    return desc.ByteWidth / elementSize(desc, format);
}

static DXGI_FORMAT textureViewFormat(DXGI_FORMAT textureFormat)
{
    switch (textureFormat)
    {
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    default:
        return textureFormat;
    }
}

static D3D11_SHADER_RESOURCE_VIEW_DESC makeSrvDesc(Resource &resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *srvDesc)
{
    if (srvDesc)
        return *srvDesc;

    auto dim = resource.dimension();
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;

    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        auto bufferDesc = resource.bufferDescriptor();
        desc.ViewDimension          = D3D11_SRV_DIMENSION_BUFFEREX;
        desc.Format                 = resource.format;
        desc.BufferEx.FirstElement  = 0;
        desc.BufferEx.NumElements   = elementCount(bufferDesc, resource.format);
        desc.BufferEx.Flags       	= 0;
    }
    else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        auto textureDesc = resource.textureDescriptor();
        desc.ViewDimension              = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Format                 	= textureViewFormat(resource.format);
        desc.Texture2D.MipLevels    	= textureDesc.MipLevels;
        desc.Texture2D.MostDetailedMip  = 0;

        if (textureDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        {
            if (textureDesc.ArraySize > 6)
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
            else
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        }
        else if (textureDesc.ArraySize > 1)
        {
            desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipLevels       = textureDesc.MipLevels;
            desc.Texture2DArray.MostDetailedMip = 0;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize       = textureDesc.ArraySize;
        }
    }
    else
    {
        check(false, "Unsupported resource kind");
    }

    return desc;
}

static D3D11_UNORDERED_ACCESS_VIEW_DESC makeUavDesc(Resource &resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *uavDesc)
{
    if (uavDesc)
        return *uavDesc;

    auto dim = resource.dimension();

    D3D11_UNORDERED_ACCESS_VIEW_DESC desc;

    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        auto bufferDesc = resource.bufferDescriptor();
        desc.ViewDimension          = D3D11_UAV_DIMENSION_BUFFER;
        desc.Format                 = resource.format;
        desc.Buffer.FirstElement    = 0;
        desc.Buffer.NumElements     = elementCount(bufferDesc, resource.format);
        desc.Buffer.Flags       	= 0;

        if (bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
        }
    }
    else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        auto textureDesc = resource.textureDescriptor();
        if (textureDesc.ArraySize > 1)
        {
            desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice        = 0;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize       = textureDesc.ArraySize;
        }
        else
        {
            auto textureDesc = resource.textureDescriptor();
            desc.ViewDimension              = D3D11_UAV_DIMENSION_TEXTURE2D;
            desc.Format                 	= textureViewFormat(resource.format);
            desc.Texture2D.MipSlice         = 0;
        }
    }
    else
    {
        check(false, "Unsupported resource kind");
    }

    return desc;
}

static D3D11_RENDER_TARGET_VIEW_DESC makeRtvDesc(Resource &resource, const D3D11_RENDER_TARGET_VIEW_DESC *rtvDesc)
{
    if (rtvDesc)
        return *rtvDesc;

    auto dim = resource.dimension();

    D3D11_RENDER_TARGET_VIEW_DESC desc;
    zero(desc);

    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        auto textureDesc = resource.textureDescriptor();
        if (textureDesc.SampleDesc.Count > 1)
        {
            desc.ViewDimension          = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        }
        else if (textureDesc.ArraySize > 1)
        {
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice        = 0;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize       = textureDesc.ArraySize;
        }
        else
        {
            desc.ViewDimension          = D3D11_RTV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice     = 0;
        }
        desc.Format                 	= resource.format;
    }
    else
    {
        check(false, "Unsupported resource kind");
    }

    return desc;
}

static D3D11_DEPTH_STENCIL_VIEW_DESC makeDsvDesc(Resource &resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *dsvDesc)
{
    if (dsvDesc)
        return *dsvDesc;

    auto dim = resource.dimension();

    D3D11_DEPTH_STENCIL_VIEW_DESC desc;
    zero(desc);

    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        auto textureDesc = resource.textureDescriptor();
        if (textureDesc.SampleDesc.Count > 1)
        {
            desc.ViewDimension          = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        }
        else if (textureDesc.ArraySize > 1)
        {
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice        = 0;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize       = textureDesc.ArraySize;
        }
        else
        {
            desc.ViewDimension          = D3D11_DSV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice     = 0;
        }
        desc.Format                 	= resource.format;
        desc.Flags                      = 0;
    }
    else
    {
        check(false, "Unsupported resource kind");
    }

    return desc;
}

void Resource::views(const D3D11_SHADER_RESOURCE_VIEW_DESC  *srvDesc,
                     const D3D11_UNORDERED_ACCESS_VIEW_DESC *uavDesc,
                     const D3D11_RENDER_TARGET_VIEW_DESC    *rtvDesc,
                     const D3D11_DEPTH_STENCIL_VIEW_DESC    *dsvDesc)
{
    srv = nullptr;
    uav = nullptr;
    rtv = nullptr;
    dsv = nullptr;

    UINT bind = 0;
    switch (dimension())
    {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        bind = bufferDescriptor().BindFlags;
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        bind = textureDescriptor().BindFlags;
        break;
    default:
        check(false, "Unsupported resource kind");
        return;
    }

    if (bind & D3D11_BIND_SHADER_RESOURCE)
    {
        auto desc = makeSrvDesc(*this, srvDesc);
        checkHR(device->CreateShaderResourceView(
            &resource(),
            &desc,
            &srv));
    }
    if (bind & D3D11_BIND_UNORDERED_ACCESS)
    {
        auto desc = makeUavDesc(*this, uavDesc);
        checkHR(device->CreateUnorderedAccessView(
            &resource(),
            &desc,
            &uav));
    }
    if (bind & D3D11_BIND_RENDER_TARGET)
    {
        auto desc = makeRtvDesc(*this, rtvDesc);
        checkHR(device->CreateRenderTargetView(
            &resource(),
            &desc,
            &rtv));
    }
    if (bind & D3D11_BIND_DEPTH_STENCIL)
    {
        auto desc = makeDsvDesc(*this, dsvDesc);
        checkHR(device->CreateDepthStencilView(
            &resource(),
            &desc,
            &dsv));
    }
}

D3D11_RESOURCE_DIMENSION Resource::dimension()
{
    D3D11_RESOURCE_DIMENSION dim;
    resource().GetType(&dim);
    return dim;
}

D3D11_BUFFER_DESC Resource::bufferDescriptor()
{
    D3D11_BUFFER_DESC desc;
    zero(desc);
    buffer->GetDesc(&desc);
    return desc;
}

D3D11_TEXTURE2D_DESC Resource::textureDescriptor()
{
    D3D11_TEXTURE2D_DESC desc;
    zero(desc);
    texture->GetDesc(&desc);
    return desc;
}

static void setResourceDebugName(ID3D11Resource &resource, const char *name)
{
    resource.SetPrivateData(WKPDID_D3DDebugObjectName,
                            static_cast<UINT>(strlen(name)),
                            name);
}

void Resource::name(const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char nameBuffer[512];
    vsprintf_s(nameBuffer, fmt, ap);
    setResourceDebugName(resource(), nameBuffer);
    va_end(ap);
}

Resource downloadForDebugging(Resource &buffer)
{
    D3D11_BUFFER_DESC desc = buffer.bufferDescriptor();

    // If the resource has a structure count, copy just that amount
    // and not everything.
    if (buffer.uav)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        buffer.uav->GetDesc(&uavDesc);
        if ((uavDesc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_APPEND) ||
            (uavDesc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_COUNTER))
        {
            check(desc.StructureByteStride > 0, "Structured buffer without struct size");

            D3D11_BUFFER_DESC counterDesc;
            zero(counterDesc);

            counterDesc.Usage               = D3D11_USAGE_DEFAULT;
            counterDesc.ByteWidth           = sizeof(uint32_t);
            counterDesc.StructureByteStride = sizeof(uint32_t);
            Resource counterCopy(counterDesc, DXGI_FORMAT_R32_UINT);

            counterDesc.Usage          = D3D11_USAGE_STAGING;
            counterDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Resource counterCopyReadback(counterDesc, DXGI_FORMAT_R32_UINT);

            context->CopyStructureCount(counterCopy.buffer, 0, buffer.uav);
            context->CopyResource(counterCopyReadback.buffer, counterCopy.buffer);

            D3D11_MAPPED_SUBRESOURCE mapped;
            zero(mapped);
            context->Map(counterCopyReadback.buffer, 0, D3D11_MAP_READ, 0, &mapped);
            uint32_t elements = *reinterpret_cast<const uint32_t *>(mapped.pData);
            context->Unmap(counterCopyReadback.buffer, 0);

            desc.ByteWidth = elements * desc.StructureByteStride;
        }
    }

    desc.Usage          = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags      = 0;
    desc.MiscFlags 		= 0;

    Resource readback(desc, buffer.format);
    D3D11_BOX srcBox;
    srcBox.left     = 0;
    srcBox.right 	= desc.ByteWidth;
    srcBox.top   	= 0;
    srcBox.bottom   = 1;
    srcBox.front    = 0;
    srcBox.back     = 1;
    context->CopySubresourceRegion(readback.buffer, 0, 0, 0, 0,
                                   buffer.buffer, 0, &srcBox);
    return readback;
}

struct PixelTranscoding
{
    UINT width;
    UINT height;
    WICPixelFormatGUID srcFormat;
    DXGI_FORMAT dstFormat;
    UINT dstChannels;
    UINT srcChannels;
    UINT channelSize;
    bool bgr;

    PixelTranscoding(UINT width, UINT height, WICPixelFormatGUID format)
        : height(height)
        , width(width)
        , srcFormat(format)
        , bgr(false)
    {
        if (srcFormat == GUID_WICPixelFormat24bppRGB)
        {
            dstFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;
            dstChannels  = 4;
            srcChannels  = 3;
            channelSize  = 1;
        }
        else if (srcFormat == GUID_WICPixelFormat24bppBGR)
        {
            dstFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;
            dstChannels  = 4;
            srcChannels  = 3;
            channelSize  = 1;
            bgr = true;
        }
        else
        {
            check(false, "Unsupported format");
        }
    }

    UINT dstStride() const
    {
        return width * dstChannels * channelSize;
    }

    UINT dstSize() const
    {
        return dstStride() * height;
    }

    UINT srcStride() const
    {
        return width * srcChannels * channelSize;
    }

    UINT srcSize() const
    {
        return srcStride() * height;
    }

    std::vector<uint8_t> transcode(const std::vector<uint8_t> &srcData)
    {
        check(srcData.size() == srcSize(), "Unexpected source size");
        std::vector<uint8_t> dstData(dstSize());

        const uint8_t *src = srcData.data();
        uint8_t *dst = dstData.data();
        const UINT pixels = width * height;

        if (srcChannels == 3 && channelSize == 1)
        {
            // 24bpp
            check(dstChannels == 4, "Unexpected destination channels");
            if (bgr)
            {
                for (UINT i = 0; i < pixels; ++i)
                {
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = -1;
                    dst += dstChannels;
                    src += srcChannels;
                }
            }
            else
            {
                for (UINT i = 0; i < pixels; ++i)
                {
                    memcpy(dst, src, 3);
                    dst[3] = -1;
                    dst += dstChannels;
                    src += srcChannels;
                }
            }
        }
        else
        {
            check(false, "Unsupported transcode");
        }

        return dstData;
    }
};

static Resource loadWICImage(const char *filename, size_t *loadedBytes)
{
    Timer t;
    CComPtr<IWICImagingFactory> factory;

    checkHR(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        reinterpret_cast<void **>(&factory)));

    CComPtr<IWICBitmapDecoder> decoder;
    {
        auto wFilename = convertToWide(filename);

        checkHR(factory->CreateDecoderFromFilename(
            wFilename.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder));
    }

    {
        UINT frames;
        checkHR(decoder->GetFrameCount(&frames));
        check(frames > 0, "No frames in the image");
    }

    CComPtr<IWICBitmapFrameDecode> firstFrame;
    checkHR(decoder->GetFrame(0, &firstFrame));

    UINT width;
    UINT height;
    WICPixelFormatGUID wicFormat;
    checkHR(firstFrame->GetSize(&width, &height));
    checkHR(firstFrame->GetPixelFormat(&wicFormat));

    PixelTranscoding transcoding(width, height, wicFormat);

    std::vector<uint8_t> wicData(transcoding.srcSize());
    checkHR(firstFrame->CopyPixels(nullptr, transcoding.srcStride(), transcoding.srcSize(), wicData.data()));

    if (loadedBytes)
        *loadedBytes += wicData.size();

    auto data = transcoding.transcode(wicData);

    D3D11_TEXTURE2D_DESC desc = texture2DDesc(width, height, transcoding.dstFormat);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData;
    initialData.pSysMem     = data.data();
    initialData.SysMemPitch = transcoding.dstStride();
    initialData.SysMemSlicePitch = initialData.SysMemPitch;

    log("    Loaded WIC \"%s\" in %.2f ms.\n", filename, t.seconds() * 1000.0);
    return Resource(desc, &initialData);
}

Resource loadPFMImage(const char *filename, FloatPixelBuffer *pixels)
{
    Timer t;
    FILE *f = nullptr;
    auto err = fopen_s(&f, filename, "rb");
    unsigned width, height;

    FloatPixelBuffer localPixels;
    if (!pixels) pixels = &localPixels;

    int srcChannels = -1;
    int dstChannels = -1;

    {
        char buf[256];
        zero(buf);
        fgets(buf, 255, f);

        if (strcmp(buf, "PF\n") == 0)
        {
            srcChannels = 3;
            dstChannels = 4;
        }
        else if (strcmp(buf, "Pf\n") == 0)
        {
            srcChannels = 1;
            dstChannels = 1;
        }
        else
        {
             check(false, "Unexpected magic header");
        }

        int numDims = fscanf_s(f, "%u %u\n", &width, &height);
        check(numDims == 2, "Unable to determine dimensions");
        check(width <= (1 << 14), "Dimension too large");
        check(height <= (1 << 14), "Dimension too large");

        fgets(buf, 255, f);
    }

    size_t numPixels = width * height;

    // fread all the image data at once to maximize I/O throughput
    std::vector<float> srcData(numPixels * srcChannels);
    {
        uint8_t *dst = reinterpret_cast<uint8_t *>(srcData.data());
        size_t bytes = numPixels * srcChannels * sizeof(float);
        while (bytes > 0)
        {
            size_t got = fread(dst, 1, bytes, f);
            check(got > 0, "Ran out of data unexpectedly");
            bytes -= got;
            dst   += got;
        }
    }
    fclose(f);

    *pixels = FloatPixelBuffer(width, height, dstChannels);
    if (srcChannels == 3)
    {
        const float *rgb = srcData.data();
        float *rgba = pixels->pixels.data();

        for (size_t i = 0; i < numPixels; ++i)
        {
            memcpy(rgba, rgb, 3 * sizeof(float));
            rgba[3] = 1.f;

            rgb  += 3;
            rgba += 4;
        }
    }
    else if (srcChannels == 1)
    {
        pixels->pixels = std::move(srcData);
    }

    D3D11_TEXTURE2D_DESC texDesc;
    zero(texDesc);
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.ArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format    = pixels->format();
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.SampleDesc.Count   = 1;
    texDesc.SampleDesc.Quality = 0;

    D3D11_SUBRESOURCE_DATA initialData;
    initialData.pSysMem          = pixels->pixels.data();
    initialData.SysMemPitch      = static_cast<UINT>(    width * pixels->channels * sizeof(float));
    initialData.SysMemSlicePitch = static_cast<UINT>(numPixels * pixels->channels * sizeof(float));

    log("    Loaded PFM \"%s\" in %.2f ms.\n", filename, t.seconds() * 1000.0);
    return Resource(texDesc, &initialData);
}

Resource loadImage(const char *filename, size_t *loadedBytes)
{
    Resource image;

    if (strstr(filename, ".pfm"))
    {
        FloatPixelBuffer pixels;
        image = loadPFMImage(filename, &pixels);
        if (loadedBytes)
            *loadedBytes += pixels.bytes();
    }
    else
    {
        image = loadWICImage(filename, loadedBytes);
    }

    return image;
}

void setRenderTarget(ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv)
{
    context->OMSetRenderTargets(1, &rtv, dsv);
    
    D3D11_TEXTURE2D_DESC texDesc;
    if (rtv)
    {
        CComPtr<ID3D11Resource> rtvResource;
        rtv->GetResource(&rtvResource);

        CComQIPtr<ID3D11Texture2D> rtvTex;
        rtvTex = rtvResource;
        rtvTex->GetDesc(&texDesc);
    }
    else if (dsv)
    {
        CComPtr<ID3D11Resource> dsvResource;
        dsv->GetResource(&dsvResource);

        CComQIPtr<ID3D11Texture2D> dsvTex;
        dsvTex = dsvResource;
        dsvTex->GetDesc(&texDesc);
    }
    else
    {
        return;
    }

    D3D11_VIEWPORT viewport;
    zero(viewport);
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.MinDepth = D3D11_MIN_DEPTH;
    viewport.MaxDepth = D3D11_MAX_DEPTH;
    viewport.Width    = static_cast<float>(texDesc.Width);
    viewport.Height   = static_cast<float>(texDesc.Height);

    context->RSSetViewports(1, &viewport);
}

void setVertexBuffers(Resource *vertexBuffer, Resource *indexBuffer)
{
    if (vertexBuffer)
    {
        UINT strides[] = {
            vertexBuffer->stride
        };
        UINT offsets[] = { 0 };
        context->IASetVertexBuffers(0, 1, bind(vertexBuffer->buffer), strides, offsets);
    }
    else
    {
        UINT strides[] = { 0 };
        UINT offsets[] = { 0 };
        ID3D11Buffer *none[] = { nullptr };
        context->IASetVertexBuffers(0, 1, none, strides, offsets);
    }

    if (indexBuffer)
    {
        context->IASetIndexBuffer(indexBuffer->buffer, indexBuffer->format, 0);
    }
    else
    {
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    }
}

XMMATRIX projection(unsigned width, unsigned height, float nearZ, float farZ, float verticalFOV, DepthMode depthMode)
{
    auto w = static_cast<float>(width);
    auto h = static_cast<float>(height);
    auto aspectRatio = w / h;

    if (depthMode == DepthMode::ForwardDepth)
        return XMMatrixPerspectiveFovRH(
            verticalFOV, aspectRatio,
            nearZ, farZ);
    else
        return XMMatrixPerspectiveFovRH(
            verticalFOV, aspectRatio,
            farZ, nearZ);
}

void GraphicsPipeline::initStates(D3D11_PRIMITIVE_TOPOLOGY topology,
                                  const D3D11_DEPTH_STENCIL_DESC * dss,
                                  const D3D11_RASTERIZER_DESC * rss,
                                  const D3D11_BLEND_DESC * bs)
{
    primitiveTopology = topology;

    if (hs && ds)
    {
        check(topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
              "Tessellation only supported for triangle lists.");
        primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    }

    if (dss)
    {
        checkHR(device->CreateDepthStencilState(dss, &depthStencilState));
    }
    else
    {
        depthStencilState = nullptr;
    }

    if (rss)
    {
        checkHR(device->CreateRasterizerState(rss, &rasterizerState));
    }
    else
    {
        rasterizerState = nullptr;
    }

    if (bs)
    {
        checkHR(device->CreateBlendState(bs, &blendState));
    }
    else
    {
        blendState = nullptr;
    }
}

void GraphicsPipeline::bind()
{
    context->VSSetShader(vs.shader, nullptr, 0);
    context->HSSetShader(hs.shader, nullptr, 0);
    context->DSSetShader(ds.shader, nullptr, 0);
    context->PSSetShader(ps.shader, nullptr, 0);
    context->IASetPrimitiveTopology(primitiveTopology);
    context->IASetInputLayout(inputLayout);
    context->RSSetState(rasterizerState);
    context->OMSetBlendState(blendState, nullptr, -1);
    context->OMSetDepthStencilState(depthStencilState, 0);
}

void GraphicsPipeline::bindWireframe()
{
    if (!rasterizerStateWireframe)
    {
        check(!!rasterizerState, "Cannot use wireframe rendering without a rasterizer state.");

        D3D11_RASTERIZER_DESC rsDesc;
        zero(rsDesc);
        rasterizerState->GetDesc(&rsDesc);
        rsDesc.CullMode = D3D11_CULL_NONE;
        rsDesc.FillMode = D3D11_FILL_WIREFRAME;

        checkHR(device->CreateRasterizerState(&rsDesc, &rasterizerStateWireframe));
    }

    if (!depthStencilStateWireframe)
    {
        if (depthStencilState)
        {
            D3D11_DEPTH_STENCIL_DESC dsDesc;
            zero(dsDesc);
            depthStencilState->GetDesc(&dsDesc);
            dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

            checkHR(device->CreateDepthStencilState(&dsDesc, &depthStencilStateWireframe));
        }
    }

    if (!psWireframe)
        psWireframe = ps;

    context->VSSetShader(vs.shader, nullptr, 0);
    context->HSSetShader(hs.shader, nullptr, 0);
    context->DSSetShader(ds.shader, nullptr, 0);
    context->PSSetShader(psWireframe.shader, nullptr, 0);
    context->IASetPrimitiveTopology(primitiveTopology);
    context->IASetInputLayout(inputLayout);
    context->RSSetState(rasterizerStateWireframe);
    context->OMSetBlendState(blendState, nullptr, -1);
    context->OMSetDepthStencilState(depthStencilStateWireframe, 0);
}

FloatPixelBuffer::FloatPixelBuffer(int width, int height, int channels)
    : width(width)
    , height(height)
    , channels(channels)
{
    pixels.resize(static_cast<size_t>(width) * height * channels, 0.f);
}

DXGI_FORMAT FloatPixelBuffer::format() const
{
    switch (channels)
    {
    case 1:
        return DXGI_FORMAT_R32_FLOAT;
    case 4:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default:
        check(false, "Invalid channel amount");
        return DXGI_FORMAT_UNKNOWN;
    }
}

size_t FloatPixelBuffer::bytes() const
{
    return sizeBytes(pixels);
}

float *FloatPixelBuffer::operator()(int x, int y)
{
    while (x < 0) x += width;
    while (y < 0) y += height;
    auto index = (static_cast<size_t>(y) * width + x) * channels;
    return &pixels[index];
}

float &FloatPixelBuffer::operator()(int x, int y, int ch)
{
    check(ch >= 0 && ch < channels, "Invalid channel");
    return operator()(x, y)[ch];
}

ConstantBuffers::CB ConstantBuffers::get(size_t size)
{
    auto sizePow2 = roundUpToPowerOf2(size);
    auto &sizeClass = sizeClasses[sizePow2];

    CB cb;

    for (auto &b : sizeClass)
    {
        // If the reference count is 1, the only reference is the
        // one in the size class vector, and the buffer is thus unused.
        if (b.use_count() == 1)
        {
            cb = b;
        }
    }

    if (!cb)
    {
        cb = std::make_shared<CComPtr<ID3D11Buffer>>();

        D3D11_BUFFER_DESC cbDesc;
        zero(cbDesc);
        cbDesc.BindFlags            = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.ByteWidth            = std::max(16U, static_cast<UINT>(sizePow2));
        cbDesc.StructureByteStride  = cbDesc.ByteWidth;
        cbDesc.MiscFlags            = 0;
        cbDesc.CPUAccessFlags       = D3D11_CPU_ACCESS_WRITE;
        cbDesc.Usage                = D3D11_USAGE_DYNAMIC;

        checkHR(device->CreateBuffer(
            &cbDesc,
            nullptr,
            &(*cb)));

        sizeClass.emplace_back(cb);
    }

    return cb;
}

D3D11_TEXTURE2D_DESC texture2DDesc(unsigned width, unsigned height, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc;
    zero(desc);
    desc.Width  = width;
    desc.Height = height;
    desc.Format = format;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    return desc;
}

D3D11_DEPTH_STENCIL_DESC depthStencilDesc(DepthMode depthMode, bool writeDepth, bool depthTest)
{
    D3D11_DEPTH_STENCIL_DESC dsDesc;
    zero(dsDesc);
    dsDesc.DepthEnable    = depthTest ? TRUE : FALSE;
    dsDesc.DepthWriteMask = writeDepth ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.StencilEnable  = FALSE;
    switch (depthMode)
    {
    case DepthMode::ForwardDepth:
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        break;
    case DepthMode::InverseDepth:
        dsDesc.DepthFunc = D3D11_COMPARISON_GREATER;
        break;
    case DepthMode::Always:
        dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        break;
    }
    return dsDesc;
}

GPUScope::GPUScope(const wchar_t *name)
    : m_annotation(annotation)
{
    if (m_annotation)
    {
        m_annotation->BeginEvent(name);
    }
}

GPUScope::~GPUScope()
{
    end();
}

void GPUScope::end()
{
    if (m_annotation)
    {
        m_annotation->EndEvent();
        m_annotation = nullptr;
    }
}

void gpuMarker(const wchar_t *fmt, ...)
{
    if (annotation)
    {
        va_list ap;
        va_start(ap, fmt);
        wchar_t marker[256];
        vswprintf_s(marker, fmt, ap);
        annotation->SetMarker(marker);
        va_end(ap);
    }
}

struct ObjFile
{
    std::vector<float3> positions;
    std::vector<float2> uvs;
    std::vector<int2> faces;
};

static ObjFile loadObj(const std::string &filename)
{
    Timer t;
    ObjFile obj;

    FILE *f = nullptr;
    fopen_s(&f, filename.c_str(), "r");

    static const int MaxLine = 4096;
    char lineBuf[MaxLine + 1];
    char *line = nullptr;

    for (;;)
    {
        line = fgets(lineBuf, MaxLine, f); 
        if (!line)
            break;

        {
            float3 p;
            if (sscanf_s(line, "v %f %f %f", &p[0], &p[1], &p[2]) == 3)
                obj.positions.emplace_back(p);
        }

        {
            float2 uv;
            if (sscanf_s(line, "vt %f %f", &uv[0], &uv[1]) == 2)
                obj.uvs.emplace_back(uv);
        }

        {
            int2 face[4] = { 0 };
            int verts = 0;
            if (sscanf_s(line, "f %d / %d   %d / %d   %d / %d   %d / %d",
                         &face[0][0], &face[0][1],
                         &face[1][0], &face[1][1],
                         &face[2][0], &face[2][1],
                         &face[3][0], &face[3][1]) == 8)
                verts = 4;
            else if (sscanf_s(line, "f %d / %d / %*d   %d / %d / %*d   %d / %d / %*d   %d / %d / %*d",
                         &face[0][0], &face[0][1],
                         &face[1][0], &face[1][1],
                         &face[2][0], &face[2][1],
                         &face[3][0], &face[3][1]) == 8)
                verts = 4;
            else if (sscanf_s(line, "f %d / %d   %d / %d   %d / %d",
                         &face[0][0], &face[0][1],
                         &face[1][0], &face[1][1],
                         &face[2][0], &face[2][1]) == 6)
                verts = 3;
            else if (sscanf_s(line, "f %d / %d / %*d   %d / %d / %*d   %d / %d / %*d",
                         &face[0][0], &face[0][1],
                         &face[1][0], &face[1][1],
                         &face[2][0], &face[2][1]) == 6)
                verts = 3;

            // Face indices are 1-based, and can contain negatives as relative accesses.
            for (auto &f : face)
            {
                if (f[0] < 0)
                    f[0] += static_cast<int32_t>(obj.positions.size());
                else
                    f[0] -= 1;

                if (f[1] < 0)
                    f[1] += static_cast<int32_t>(obj.uvs.size());
                else
                    f[1] -= 1;
            }

            if (verts == 3)
            {
                obj.faces.emplace_back(face[0]);
                obj.faces.emplace_back(face[1]);
                obj.faces.emplace_back(face[2]);
            }
            else if (verts == 4)
            {
                obj.faces.emplace_back(face[0]);
                obj.faces.emplace_back(face[1]);
                obj.faces.emplace_back(face[2]);

                obj.faces.emplace_back(face[0]);
                obj.faces.emplace_back(face[2]);
                obj.faces.emplace_back(face[3]);
            }
        }
    }

    fclose(f);

    if (!obj.faces.empty())
    {
        log("Loaded %u triangles from \"%s\" in %.2f ms\n",
            static_cast<unsigned>(obj.faces.size() / 3), filename.c_str(),
            t.seconds() * 1000.0);
    }

    return obj;
}

struct VertexHash
{
    // Count the position and UVs in the hash, but not normals,
    // since they will be computed by us.
    size_t operator()(const Vertex &v) const
    {
        std::hash<float> h;
        return h(v.pos[0])
            ^ h(v.pos[1])
            ^ h(v.pos[2])
            ^ h(v.uv[0])
            ^ h(v.uv[1]);
    }
};

static XMVECTOR loadFloat2(const float2 &fs)
{
    static_assert(sizeof(XMFLOAT2) == sizeof(float2), "Size mismatch");
    return XMLoadFloat2(reinterpret_cast<const XMFLOAT2 *>(fs.data()));
}

static XMVECTOR loadFloat3(const float3 &fs)
{
    static_assert(sizeof(XMFLOAT3) == sizeof(float3), "Size mismatch");
    return XMLoadFloat3(reinterpret_cast<const XMFLOAT3 *>(fs.data()));
}

static void storeFloat3(float3 &fs, XMVECTOR v)
{
    static_assert(sizeof(XMFLOAT3) == sizeof(float3), "Size mismatch");
    XMStoreFloat3(reinterpret_cast<XMFLOAT3 *>(fs.data()), v);
}

void computeVertexNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    Timer t;

    std::vector<unsigned> vertexTriangleAmount(vertices.size(), 0);

    {
        float3 zero { 0, 0, 0 };
        for (auto &v : vertices)
            v.normal = zero;
    }

    // Accumulate to each vertex the sum of the normals of
    // each triangle that vertex belongs to.
    size_t lastTriangleStart = indices.size() - 3;
    for (size_t i = 0; i <= lastTriangleStart; i += 3)
    {
        auto i0 = indices[i + 0];
        auto i1 = indices[i + 1];
        auto i2 = indices[i + 2];
        
        auto &v0 = vertices[i0];
        auto &v1 = vertices[i1];
        auto &v2 = vertices[i2];

        auto &a0 = vertexTriangleAmount[i0];
        auto &a1 = vertexTriangleAmount[i1];
        auto &a2 = vertexTriangleAmount[i2];

        auto A = loadFloat3(v0.pos);
        auto B = loadFloat3(v1.pos);
        auto C = loadFloat3(v2.pos);

        // Assume counter-clockwise winding here

        auto AB = XMVectorSubtract(B, A);
        auto AC = XMVectorSubtract(C, A);

        auto N = XMVector3Cross(AB, AC);
        N = XMVector3Normalize(N);

        storeFloat3(v0.normal, XMVectorAdd(N, loadFloat3(v0.normal)));
        storeFloat3(v1.normal, XMVectorAdd(N, loadFloat3(v1.normal)));
        storeFloat3(v2.normal, XMVectorAdd(N, loadFloat3(v2.normal)));
        ++a0;
        ++a1;
        ++a2;
    }

    // Divide and renormalize the sums to obtain final vertex normals.
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        auto &v = vertices[i];
        auto a  = vertexTriangleAmount[i];

        auto invAmount = XMVectorReplicate(1.f / static_cast<float>(a));
        auto average   = XMVectorMultiply(invAmount, loadFloat3(v.normal));
        auto N         = XMVector3Normalize(average);
        storeFloat3(v.normal, N);
    }

    log("Computed vertex normals for %u vertices (%u triangles) in %.2f ms.\n",
        static_cast<unsigned>(vertices.size()),
        static_cast<unsigned>(indices.size() / 3),
        t.seconds() * 1000.0);
}

void computeTessellationFactors(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, float tessellationTriangleArea)
{
    for (auto &v : vertices)
        v.tessellation = 1;

    if (tessellationTriangleArea <= 0)
        return;

    size_t lastTriangleStart = indices.size() - 3;
    for (size_t i = 0; i <= lastTriangleStart; i += 3)
    {
        auto i0 = indices[i + 0];
        auto i1 = indices[i + 1];
        auto i2 = indices[i + 2];
        
        auto &v0 = vertices[i0];
        auto &v1 = vertices[i1];
        auto &v2 = vertices[i2];

        auto uv0 = loadFloat2(v0.uv);
        auto uv1 = loadFloat2(v1.uv);
        auto uv2 = loadFloat2(v2.uv);

        auto u = XMVectorSubtract(uv1, uv0);
        auto v = XMVectorSubtract(uv2, uv0);

        // A = 1/2 * |u x v|
        auto uxv            = XMVector3Cross(u, v);
        auto normUxV        = XMVector3Length(uxv);
        auto triangleUVArea = 0.5f * XMVectorGetX(normUxV);

        float tessellation;

        if (triangleUVArea <= 0)
        {
            tessellation = 1;
        }
        else
        {
            // With a tessellation factor of 2, we would divide the area by 4, so take the square root
            // of the area ratio.
            float areaRatio = triangleUVArea / tessellationTriangleArea;
            tessellation = std::sqrt(areaRatio);
        }

        // The tessellation factor for each vertex is the max of all the triangles it
        // belongs to.
        v0.tessellation = std::max(v0.tessellation, tessellation);
        v1.tessellation = std::max(v1.tessellation, tessellation);
        v2.tessellation = std::max(v2.tessellation, tessellation);
    }

    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::min();
    double avg = 0;

    for (auto &v : vertices)
    {
        min = std::min<double>(min, v.tessellation);
        max = std::max<double>(max, v.tessellation);
        avg += v.tessellation;
    }

    avg /= static_cast<double>(vertices.size());

    log("Tessellation min/avg/max: %f / %f / %f\n", min, avg, max);
}

Mesh loadMesh(const std::vector<std::string> &objFilenames,
              MeshLoadMode loadMode,
              float tessellationTriangleArea)
{
    Timer t;

    size_t approxTotalVerts   = 0;
    size_t approxTotalIndices = 0;

    Mesh m;
    m.objFiles = objFilenames;

    std::vector<ObjFile> objs;
    for (auto &f : objFilenames)
    {
        // Use the last directory name as the name of the mesh.
        auto parts = splitPath(f);
        if (m.name.empty() && parts.size() > 1)
            m.name = parts[parts.size() - 2];

        objs.emplace_back(loadObj(f));
        approxTotalVerts   += objs.back().positions.size();
        approxTotalIndices += objs.back().faces.size();
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(approxTotalVerts * 3 / 2);
    indices.reserve(approxTotalIndices * 3);

    float scale = 0;

    for (auto &o : objs)
    {
        std::unordered_map<Vertex, uint32_t, VertexHash> vertexIndices(o.positions.size());

        for (auto &f : o.faces)
        {
            auto &pos = o.positions[f[0]];
            auto &uv  = o.uvs[f[1]];

            float x = pos[0];
            float y = pos[1];
            float z = pos[2];

            if (loadMode == MeshLoadMode::SwapYZ)
            {
                std::swap(y, z);
                y *= -1;
            }

            Vertex v;
            v.pos[0] = x;
            v.pos[1] = y;
            v.pos[2] = z;
            v.pos[3] = 1;
            v.uv     = uv;
            v.normal[0] = 0;
            v.normal[1] = 0;
            v.normal[2] = 0;
            v.tessellation = 0;

            float distanceFromOrigin = sqrt(x*x + y*y + z*z);
            scale = std::max(scale, distanceFromOrigin);

            uint32_t idx;

            if (vertexIndices.count(v) == 0)
            {
                idx = static_cast<uint32_t>(vertices.size());
                vertices.emplace_back(v);
                vertexIndices[v] = idx;
            }
            else
            {
                idx = vertexIndices[v];
            }

            indices.emplace_back(idx);
        }
    }

    computeTessellationFactors(vertices, indices, tessellationTriangleArea);
    computeVertexNormals(vertices, indices);

    m.vertexAmount = static_cast<unsigned>(vertices.size());
    m.indexAmount  = static_cast<unsigned>(indices.size());
    m.indexFormat  = DXGI_FORMAT_R32_UINT;
    m.inputLayoutDesc = Vertex::inputLayoutDesc();
    m.scale = scale;

    {
        D3D11_BUFFER_DESC vbDesc;
        zero(vbDesc);
        vbDesc.ByteWidth           = static_cast<UINT>(sizeBytes(vertices));
        vbDesc.StructureByteStride = sizeof(Vertex);
        vbDesc.Usage               = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
        m.vertexBuffer = Resource(vbDesc, DXGI_FORMAT_UNKNOWN, vertices.data(), sizeBytes(vertices));
    }

    {
        D3D11_BUFFER_DESC ibDesc;
        zero(ibDesc);
        ibDesc.ByteWidth = static_cast<UINT>(sizeBytes(indices));
        ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        m.indexBuffer = Resource(ibDesc, DXGI_FORMAT_R32_UINT, indices.data(), sizeBytes(indices));
    }

    log("Loaded mesh with %u vertices and %u indices (%u triangles) in %.2f ms.\n",
        m.vertexAmount, m.indexAmount, m.indexAmount / 3, t.seconds() * 1000.0);

    return m;
}

CComPtr<ID3D11SamplerState> samplerPoint(D3D11_TEXTURE_ADDRESS_MODE mode)
{
    CComPtr<ID3D11SamplerState> bilinear;
    D3D11_SAMPLER_DESC bilinearDesc;
    zero(bilinearDesc);
    bilinearDesc.AddressU = mode;
    bilinearDesc.AddressV = mode;
    bilinearDesc.AddressW = mode;
    bilinearDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    bilinearDesc.MinLOD   = 0;
    bilinearDesc.MaxLOD   = D3D11_FLOAT32_MAX;
    checkHR(device->CreateSamplerState(&bilinearDesc, &bilinear));
    return bilinear;
}

CComPtr<ID3D11SamplerState> samplerBilinear(D3D11_TEXTURE_ADDRESS_MODE mode)
{
    CComPtr<ID3D11SamplerState> bilinear;
    D3D11_SAMPLER_DESC bilinearDesc;
    zero(bilinearDesc);
    bilinearDesc.AddressU = mode;
    bilinearDesc.AddressV = mode;
    bilinearDesc.AddressW = mode;
    bilinearDesc.Filter   = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    bilinearDesc.MinLOD   = 0;
    bilinearDesc.MaxLOD   = D3D11_FLOAT32_MAX;
    checkHR(device->CreateSamplerState(&bilinearDesc, &bilinear));
    return bilinear;
}

CComPtr<ID3D11SamplerState> samplerAnisotropic(unsigned maxAnisotropy, D3D11_TEXTURE_ADDRESS_MODE mode)
{
    CComPtr<ID3D11SamplerState> aniso;
    D3D11_SAMPLER_DESC anisoDesc;
    zero(anisoDesc);
    anisoDesc.AddressU = mode;
    anisoDesc.AddressV = mode;
    anisoDesc.AddressW = mode;
    anisoDesc.Filter   = D3D11_FILTER_ANISOTROPIC;
    anisoDesc.MaxAnisotropy = maxAnisotropy;
    anisoDesc.MinLOD   = 0;
    anisoDesc.MaxLOD   = D3D11_FLOAT32_MAX;
    checkHR(device->CreateSamplerState(&anisoDesc, &aniso));
    return aniso;
}

XMMATRIX cubeMapFaceViewRH(CubeMapFace face, XMVECTOR eyePosition)
{
    XMVECTOR eyeDirection;
    XMVECTOR up;

    // +X: up = +y, right = -z
    // -X: up = +y, right = +z
    // +Y: up = -z, right = +x
    // -Y: up = +z, right = +x
    // +Z: up = +y, right = +x
    // -Z: up = +y, right = -x
    switch (face)
    {
    case CubeMapFace::XPositive:
        eyeDirection = XMVectorSet( 1,  0,  0, 0);
        up           = XMVectorSet( 0,  1,  0, 0);
        break;
    case CubeMapFace::XNegative:
        eyeDirection = XMVectorSet(-1,  0,  0, 0);
        up           = XMVectorSet( 0,  1,  0, 0);
        break;
    case CubeMapFace::YPositive:
        eyeDirection = XMVectorSet( 0,  1,  0, 0);
        up           = XMVectorSet( 0,  0, -1, 0);
        break;
    case CubeMapFace::YNegative:
        eyeDirection = XMVectorSet( 0, -1,  0, 0);
        up           = XMVectorSet( 0,  0,  1, 0);
        break;
    case CubeMapFace::ZPositive:
        eyeDirection = XMVectorSet( 0,  0,  1, 0);
        up           = XMVectorSet( 0,  1,  0, 0);
        break;
    case CubeMapFace::ZNegative:
        eyeDirection = XMVectorSet( 0,  0, -1, 0);
        up           = XMVectorSet( 0,  1,  0, 0);
        break;
    }

    return XMMatrixLookToRH(eyePosition, eyeDirection, up);
}

XMMATRIX cubeMapFaceProjRH(float nearZ, float farZ, DepthMode depthMode)
{
    // Cube map faces are always square and have 90 degrees FOV
    // Projection is left-handed because the view is also
    static const float fov = XM_PI / 2.f;
    if (depthMode == DepthMode::ForwardDepth)
        return XMMatrixPerspectiveFovRH(fov, 1.f, nearZ, farZ);
    else
        return XMMatrixPerspectiveFovRH(fov, 1.f, farZ, nearZ);
}

Oculus::Oculus(unsigned width, unsigned height)
{
    mirrorW = width;
    mirrorH = height;

    ovrInitParams initParams;
    zero(initParams);

    session = nullptr;

    ovrResult result = ovr_Initialize(&initParams);
    if (OVR_SUCCESS(result))
    {
        result = ovr_Create(&session, &luid);
        if (!OVR_SUCCESS(result) || !session)
            session = nullptr;
    }

    if (!session)
    {
        log("Oculus Rift not found. VR rendering disabled.\n");
        return;
    }

    zero(hmd);
    hmd = ovr_GetHmdDesc(session);

    log("Oculus Rift found. %s %s (%d x %d @ %4.1f Hz).\n",
        hmd.Manufacturer,
        hmd.ProductName,
        hmd.Resolution.w,
        hmd.Resolution.h,
        hmd.DisplayRefreshRate);

    mirrorTexture = nullptr;
    sensorSampleTime = -12345;
}

Oculus::~Oculus()
{
    if (!isConnected()) return;

    eyes.clear();

    if (mirrorTexture)
        ovr_DestroyMirrorTexture(session, mirrorTexture);

    if (session)
        ovr_Destroy(session);

    ovr_Shutdown();
}

void Oculus::assertStatus(ovrResult result)
{
    if (OVR_SUCCESS(result))
        return;

    ovrErrorInfo error;
    ovr_GetLastErrorInfo(&error);

    check(false, "OVR error: %s", error.ErrorString);
}

void Oculus::createOutputTextures(DXGI_FORMAT format)
{
    eyes.clear();

    if (!isConnected())
        return;

    eyes.resize(2);

    for (unsigned eye = 0; eye < 2; ++eye)
    {
        auto &e = eyes[eye];
        e.number = eye;
        e.session = session;
        e.fov = hmd.DefaultEyeFov[eye];
        e.size = ovr_GetFovTextureSize(session, static_cast<ovrEyeType>(eye), e.fov, 1.f);

        D3D11_TEXTURE2D_DESC desc = texture2DDesc(e.size.w, e.size.h, format);
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        assertStatus(ovr_CreateSwapTextureSetD3D11(session, device, &desc, 0, &e.swapTextureSet));

        e.swapTargets.resize(e.swapTextureSet->TextureCount);
        for (int t = 0; t < e.swapTextureSet->TextureCount; ++t)
        {
            auto &target = e.swapTargets[t];

			auto ovrTex = reinterpret_cast<ovrD3D11Texture *>(&e.swapTextureSet->Textures[t])->D3D11.pTexture;
            target = Resource(CComPtr<ID3D11Texture2D>(ovrTex));
            target.views();
            target.name("Eye #%u swap texture #%d", eye, t);
        }

        D3D11_TEXTURE2D_DESC zDesc = texture2DDesc(
            e.size.w,
            e.size.h,
            DXGI_FORMAT_D32_FLOAT);
        zDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        e.depthBuffer = Resource(zDesc);
        RESOURCE_DEBUG_NAME(e.depthBuffer);

        e.renderDesc = ovr_GetRenderDesc(session,
                                         (eye == 0) ? ovrEye_Left : ovrEye_Right,
                                         e.fov);
    }

    D3D11_TEXTURE2D_DESC desc = texture2DDesc(mirrorW, mirrorH, format);
    desc.BindFlags = 0;

    assertStatus(ovr_CreateMirrorTextureD3D11(session, device, &desc, 0, &mirrorTexture));
}

ID3D11Texture2D *Oculus::mirrorD3DTexture()
{
    return reinterpret_cast<ovrD3D11Texture *>(mirrorTexture)->D3D11.pTexture;
}

bool Oculus::isConnected() const
{
    return !!session;
}

bool Oculus::isActive() const
{
    if (!isConnected()) return false;

    ovrSessionStatus status;
    assertStatus(ovr_GetSessionStatus(session, &status));
    return status.HasVrFocus && status.HmdPresent;
}

void Oculus::samplePose()
{
    double predictedFrameTime = ovr_GetPredictedDisplayTime(session, 0);
    sensorSampleTime          = ovr_GetTimeInSeconds();
    auto hmdState             = ovr_GetTrackingState(session, predictedFrameTime, ovrTrue);

    ovrPosef    eyePoses[2];
    ovrVector3f eyeOffsets[2] = {
        eyes[0].renderDesc.HmdToEyeViewOffset,
        eyes[1].renderDesc.HmdToEyeViewOffset,
    };

    ovr_CalcEyePoses(hmdState.HeadPose.ThePose, eyeOffsets, eyePoses);

    eyes[0].pose = eyePoses[0];
    eyes[1].pose = eyePoses[1];
}

void Oculus::recenter()
{
    ovr_RecenterPose(session);
}

ovrLayerEyeFov Oculus::frame()
{
    ovrLayerEyeFov f = {};

    f.Header.Type = ovrLayerType_EyeFov;
    f.Header.Flags = 0;

    check(sensorSampleTime >= 0, "Sensors were never sampled.");

    for (unsigned eye = 0; eye < 2; ++eye)
    {
        auto &e = eyes[eye];

        f.ColorTexture[eye]   = e.swapTextureSet;
        f.Fov[eye]            = e.fov;
        f.Viewport[eye].Pos.x = 0;
        f.Viewport[eye].Pos.y = 0;
        f.Viewport[eye].Size  = e.size;
        f.RenderPose[eye]     = e.pose;
        f.SensorSampleTime    = sensorSampleTime;
    }

    return f;
}

Oculus::Eye::~Eye()
{
    swapTargets.clear();

    if (swapTextureSet)
        ovr_DestroySwapTextureSet(session, swapTextureSet);
}

Resource &Oculus::Eye::active()
{
    return swapTargets[swapTextureSet->CurrentIndex];
}

void Oculus::Eye::next()
{
    swapTextureSet->CurrentIndex =
        (swapTextureSet->CurrentIndex + 1) % swapTextureSet->TextureCount;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> Vertex::inputLayoutDesc()
{
    return {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,                            0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // tessellation factor
    };
}
