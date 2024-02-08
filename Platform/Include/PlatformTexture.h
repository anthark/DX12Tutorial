#pragma once

template <typename T>
struct Point2;
using Point2i = Point2<int>;

#include "PlatformDevice.h"

namespace Platform
{

struct CreateTextureParams
{
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool enableRT = false;
    bool enableUAV = false;
    bool enableDS = false;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr;

    UINT arraySize = 1;
    UINT mips = 1;
};

PLATFORM_API bool CreateTexture(const CreateTextureParams& params, bool generateMips, Device* pDevice, Platform::GPUResource& textureResource, const void* pInitialData = nullptr, size_t initialDataSize = 0);
PLATFORM_API bool CreateTextureFromFile(LPCTSTR filename, Device* pDevice, Platform::GPUResource& textureResource, bool srgb = false);
PLATFORM_API bool CreateTextureArrayFromFile(LPCTSTR filename, const Point2i& grid, Device* pDevice, ID3D12GraphicsCommandList* pUploadCommandList, Platform::GPUResource& textureResource, bool srgb = false);

} // Platform
