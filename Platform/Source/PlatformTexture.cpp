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
    if (mipCount > 2)
    {
        mipCount -= 2; // Skip last two mips, as texture cannot be less than 4x4 pixels
    }

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

            bool res = false;
            if (image.height == 1)
            {
                res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Tex1D(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, image.width, 1, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, textureResource, pBuffer, dataSize);
            }
            else
            {
                res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Tex2D(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height, 1, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, textureResource, pBuffer, dataSize);
            }

            delete[] pBuffer;
            pBuffer = nullptr;

            return res;
        }
    }

    return false;
}

bool CreateTextureArrayFromFile(LPCTSTR filename, const Point2i& grid, Device* pDevice, ID3D12GraphicsCommandList* pUploadCommandList, Platform::GPUResource& textureResource, bool srgb)
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
            UINT initialDataSize = image.height * PNG_IMAGE_ROW_STRIDE(image) * PNG_IMAGE_PIXEL_SIZE(image.format);
            UINT8* pBuffer = new UINT8[initialDataSize];
            pngRes = png_image_finish_read(&image, NULL, pBuffer, 0, NULL);
            UINT imagePitch = PNG_IMAGE_ROW_STRIDE(image);
            UINT imagePixelSize = PNG_IMAGE_PIXEL_SIZE(image.format);

            bool res = true;
            if (pngRes != 0)
            {
                png_image imageTile = image;
                imageTile.width /= grid.x;
                imageTile.height /= grid.y;
                UINT tilePitch = PNG_IMAGE_ROW_STRIDE(imageTile);

                UINT mips = 0;
                size_t tileDataSize = CalculateSizeWithMips(imageTile, mips);

                res = pDevice->CreateGPUResource(
                    CD3DX12_RESOURCE_DESC::Tex2D(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, imageTile.width, imageTile.height, grid.x * grid.y, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, textureResource
                );
                if (res)
                {
                    UINT8* pTileBuffer = new UINT8[tileDataSize];
                    for (int j = 0; j < grid.y && res; j++)
                    {
                        for (int i = 0; i < grid.x && res; i++)
                        {
                            const UINT8* pTileSrc = &pBuffer[j*imageTile.height*imagePitch + i * imageTile.width * imagePixelSize];
                            UINT8* pTileDst = pTileBuffer;
                            for (UINT k = 0; k < imageTile.height; k++)
                            {
                                memcpy(pTileDst, pTileSrc, tilePitch);
                                pTileDst += tilePitch;
                                pTileSrc += imagePitch;
                            }
                            GenerateMips(pTileBuffer, imageTile, mips - 1);

                            res = SUCCEEDED(pDevice->UpdateTexture(pUploadCommandList, textureResource.pResource, pTileBuffer, tileDataSize, (j * grid.x + i) * mips));
                        }
                    }
                    delete[] pTileBuffer;
                    pTileBuffer = nullptr;
                }
            }

            delete[] pBuffer;
            pBuffer = nullptr;

            return res;
        }
    }

    return false;
}

void CalcHistogram(LPCTSTR filename)
{
    UINT count[256] = {0};
    Point3f color[256] = {0};

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
            UINT pixelSize = PNG_IMAGE_PIXEL_SIZE(image.format);

            size_t dataSize = image.height * pitch;

            UINT8* pBuffer = new UINT8[dataSize];

            pngRes = png_image_finish_read(&image, NULL, pBuffer, 0, NULL);

            if (pngRes != 0)
            {
                const UINT8* pPixel = nullptr;
                for (UINT j = 0; j < image.height; j++)
                {
                    for (UINT i = 0; i < image.width; i++)
                    {
                        pPixel = ((const UINT8*)pBuffer) + pitch * j + i * pixelSize;
                        float luminance = 0.2126f*pPixel[0] + 0.7152f*pPixel[1] + 0.0722f*pPixel[2];

                        UINT8 byteLum = (UINT8)luminance;

                        ++count[byteLum];
                        color[byteLum] = color[byteLum] + Point3f{(float)pPixel[0], (float)pPixel[1], (float)pPixel[2]} * (1.0f/255.0f);
                    }
                }

                for (int i = 0; i <= 255; i++)
                {
                    if (count[i] > 0)
                    {
                        color[i] = color[i] * (1.0f / count[i]);
                    }
                }
            }

            {
                png_image dstImage;
                memset(&dstImage, 0, sizeof(dstImage));
                dstImage.version = PNG_IMAGE_VERSION;
                dstImage.width = 256;
                dstImage.height = 1;
                dstImage.format = PNG_FORMAT_RGBA;

                UINT8* pDstBuffer = new UINT8[1024];
                for (int i = 0; i < 256; i++)
                {
                    pDstBuffer[i * 4 + 0] = (UINT8)(color[i].x * 255);
                    pDstBuffer[i * 4 + 1] = (UINT8)(color[i].y * 255);
                    pDstBuffer[i * 4 + 2] = (UINT8)(color[i].z * 255);
                    pDstBuffer[i * 4 + 3] = 255;
                }

                pngRes = png_image_write_to_file(&dstImage, "palette.png", 0, pDstBuffer, 256 * 4, nullptr);

                delete[] pDstBuffer;
            }

            delete[] pBuffer;
            pBuffer = nullptr;
        }
    }
}

} // Platform
