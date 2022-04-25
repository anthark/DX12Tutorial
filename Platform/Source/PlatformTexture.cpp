#include "stdafx.h"
#include "PlatformTexture.h"

#include <algorithm>

#include "png.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"

namespace
{

size_t CalculateSizeWithMips(UINT32 width, UINT32 height, size_t stride, UINT32 componentSize, UINT& mipCount)
{
    DWORD mips = 0;
    _BitScanForward(&mips, std::min(NearestPowerOf2(height), NearestPowerOf2(width)));

    mipCount = (UINT)mips + 1;
    mipCount -= 2; // Skip last two mips, as texture cannot be less than 4x4 pixels

    size_t res = 0;
    for (UINT i = 0; i < mipCount; i++)
    {
        res += componentSize * height * stride;
        height /= 2;
        stride /= 2;
    }

    return res;
}

size_t CalculateSizeWithMips(const png_image& image, UINT& mipCount)
{
    return CalculateSizeWithMips(image.width, image.height, PNG_IMAGE_ROW_STRIDE(image), PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format), mipCount);
}

void GenerateMips(void* pInitialData, UINT32 width, UINT32 height, size_t stride, UINT32 componentSize, UINT mipsToGenerate)
{
    UINT8* pData = static_cast<UINT8*>(pInitialData);

    for (UINT i = 0; i < mipsToGenerate; i++)
    {
        const UINT8* pSrcData = pData;
        pData += componentSize * height * stride;
        height /= 2;
        width /= 2;
        stride /= 2;

        for (int y = 0; y < (int)height; y++)
        {
            for (int x = 0; x < (int)width; x++)
            {
                for (int i = 0; i < 4; i++)
                {
                    int accum = 0;

                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 0) + (x * 2 + 0) * 4) * componentSize + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 1) + (x * 2 + 0) * 4) * componentSize + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 0) + (x * 2 + 1) * 4) * componentSize + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 1) + (x * 2 + 1) * 4) * componentSize + i);
                    accum /= 4;

                    pData[(stride * y + x * 4) * componentSize + i] = (UINT8)accum;
                }
            }
        }
    }
}

void GenerateMips(void* pInitialData, const png_image& image, UINT mipsToGenerate)
{
    assert(image.format == PNG_FORMAT_RGBA);

    return GenerateMips(pInitialData, image.width, image.height, PNG_IMAGE_ROW_STRIDE(image), PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format), mipsToGenerate);
}

}

namespace Platform
{

bool CreateTexture(const CreateTextureParams& params, bool generateMips, Device* pDevice, Platform::GPUResource& textureResource, const void* pInitialData, size_t initialDataSize)
{
    if (generateMips)
    {
        assert(params.format == DXGI_FORMAT_R8G8B8A8_UNORM || params.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

        UINT mips = 0;
        size_t dataSize = CalculateSizeWithMips(params.width, params.height, params.width * 4, 1, mips);
        UINT8* pBuffer = new UINT8[dataSize];
        memcpy(pBuffer, pInitialData, initialDataSize);

        GenerateMips(pBuffer, params.width, params.height, params.width * 4, 1, mips - 1);

        HRESULT hr = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Tex2D(params.format, params.width, params.height, 1, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, textureResource, pBuffer, dataSize);

        delete[] pBuffer;
        pBuffer = nullptr;

        return SUCCEEDED(hr);
    }
    else
    {
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (params.enableRT)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (params.enableUAV)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (params.enableDS)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(params.format, params.width, params.height, params.arraySize, params.mips, 1, 0, flags);
        HRESULT hr = pDevice->CreateGPUResource(desc, params.initialState, params.pOptimizedClearValue, textureResource, pInitialData, initialDataSize);
        return SUCCEEDED(hr);
    }

    return true;
}

bool CreateTextureFromFile(LPCTSTR filename, Device* pDevice, Platform::GPUResource& textureResource, bool srgb)
{
    std::vector<char> data;
    if (Platform::ReadFileContent(filename, data))
    {
        png_image image;
        memset(&image, 0, sizeof(png_image));
        image.version = PNG_IMAGE_VERSION;

        int pngRes = png_image_begin_read_from_memory(&image, &data[0], data.size());
        assert(pngRes != 0);

        if (pngRes != 0)
        {
            UINT pitch = PNG_IMAGE_ROW_STRIDE(image);

            UINT mips = 0;
            size_t dataSize = CalculateSizeWithMips(image, mips);

            UINT8* pBuffer = new UINT8[dataSize];

            pngRes = png_image_finish_read(&image, NULL, pBuffer, 0, NULL);

            GenerateMips(pBuffer, image, mips - 1);

            HRESULT hr = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Tex2D(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height, 1, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, textureResource, pBuffer, dataSize);

            delete[] pBuffer;
            pBuffer = nullptr;

            return SUCCEEDED(hr);
        }
    }

    return false;
}

} // Platform
