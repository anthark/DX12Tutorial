#include "stdafx.h"
#include "PlatformDevice.h"

#include "Platform.h"
#include "PlatformIO.h"
#include "PlatformCommandQueue.h"
#include "PlatformRingBuffer.h"

#include "D3D12MemAlloc.h"

#include "pix3.h"

#include <set>

#define RELEASE(a)\
if ((a) != nullptr)\
{\
    (a)->Term();\
    delete (a);\
    (a) = nullptr;\
}

namespace
{

class D3DInclude : public ID3DInclude
{
    HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
    {
        std::vector<char> data;
        bool res = true;

        std::string filename = pFileName;
        if (IncludeType == D3D_INCLUDE_SYSTEM)
        {
            filename = "../Common/Shaders/" + filename;
        }

#ifdef _UNICODE
        if (res)
        {
            size_t count = filename.length();
            wchar_t* pBuffer = new wchar_t[count + 1];
            size_t converted = 0;
            res = mbstowcs_s(&converted, pBuffer, count + 1, filename.c_str(), count) == 0 && converted == count + 1;
            if (res)
            {
                includeFiles.insert(pBuffer);

                res = Platform::ReadFileContent(pBuffer, data);
            }

            delete[] pBuffer;
        }
#else
        res = Platform::ReadFileContent(filename.c_str(), data);
        if (res)
        {
            includeFiles.insert(filename);
        }
#endif
        if (res)
        {
            LPVOID pData = malloc(data.size());
            memcpy(pData, data.data(), data.size());

            *ppData = pData;
            *pBytes = (UINT)data.size();
        }

        assert(res);

        return res ? S_OK : E_FAIL;
    }

    HRESULT Close(LPCVOID pData)
    {
        free(const_cast<void*>(pData));

        return S_OK;
    }

public:
    std::set<std::tstring> includeFiles;
};

}

namespace Platform
{

PixEvent::PixEvent(ID3D12GraphicsCommandList* pCmdList, UINT64 color, LPCTSTR name)
{
    m_pCmdList = pCmdList;
    PIXBeginEvent(pCmdList, color, name);
}

PixEvent::~PixEvent()
{
    PIXEndEvent(m_pCmdList);
}

struct DescriptorRingBuffer : public RingBuffer<DescriptorRingBuffer, D3D12_GPU_DESCRIPTOR_HANDLE>
{
    DescriptorRingBuffer() : RingBuffer<DescriptorRingBuffer, D3D12_GPU_DESCRIPTOR_HANDLE>(), pDescHeap(nullptr), descSize(0) {}

    bool Init(ID3D12Device* pDevice, UINT descCount, ID3D12DescriptorHeap* pHeap)
    {
        HRESULT hr = S_OK;

        pDescHeap = pHeap;

        if (SUCCEEDED(hr))
        {
            descSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            return RingBuffer<DescriptorRingBuffer, D3D12_GPU_DESCRIPTOR_HANDLE>::Init(descCount);
        }

        return false;
    }

    void Term()
    {
        RingBuffer<DescriptorRingBuffer, D3D12_GPU_DESCRIPTOR_HANDLE>::Term();

        pDescHeap = nullptr;
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE At(UINT64 idx) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = pDescHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += idx * descSize;
        return handle;
    }

    inline D3D12_CPU_DESCRIPTOR_HANDLE AtCPU(UINT64 idx) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = pDescHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += idx * descSize;
        return handle;
    }

    inline ID3D12DescriptorHeap* GetHeap() const { return pDescHeap; }

private:
    ID3D12DescriptorHeap* pDescHeap;
    UINT descSize;
};

struct HeapRingBuffer : public RingBuffer<HeapRingBuffer, UINT8*>
{
    HeapRingBuffer() : RingBuffer<HeapRingBuffer, UINT8*>(), pDataBuffer(nullptr), pUploadData(nullptr) {}

    bool Init(ID3D12Device* pDevice, D3D12_HEAP_TYPE heapType, UINT64 heapSize)
    {
        HRESULT hr = S_OK;

        // Create upload buffer
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = heapType;

        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        switch (heapType)
        {
            case D3D12_HEAP_TYPE_UPLOAD:
                state = D3D12_RESOURCE_STATE_GENERIC_READ;
                break;

            case D3D12_HEAP_TYPE_READBACK:
                state = D3D12_RESOURCE_STATE_COPY_DEST;
                break;
        }

        D3D_CHECK(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer({ heapSize }), state, nullptr, __uuidof(ID3D12Resource), (void**)&pDataBuffer));

        D3D12_RANGE range = {};
        range.Begin = 0;
        range.End = heapSize;
        D3D_CHECK(pDataBuffer->Map(0, &range, (void**)&pUploadData));

        if (SUCCEEDED(hr))
        {
            return RingBuffer<HeapRingBuffer, UINT8*>::Init(heapSize);
        }

        return false;
    }

    void Term()
    {
        RingBuffer<HeapRingBuffer, UINT8*>::Term();

        if (pUploadData != nullptr)
        {
            D3D12_RANGE range = {};
            range.Begin = 0;
            range.End = allocMaxSize;
            pDataBuffer->Unmap(0, &range);
            pUploadData = nullptr;
        }

        D3D_RELEASE(pDataBuffer);
    }

    inline UINT8* At(UINT64 idx) const { return pUploadData + idx; }
    inline ID3D12Resource* GetBuffer() const { return pDataBuffer; }

private:
    ID3D12Resource* pDataBuffer;
    UINT8* pUploadData;
};

struct QueryRingBuffer : public RingBuffer<QueryRingBuffer, std::function<void(UINT64)>>
{
    QueryRingBuffer() : RingBuffer<QueryRingBuffer, std::function<void(UINT64)>>(), pGPUHeap(nullptr), prevAllocEnd(0), heapSize(0), cpuBuffer(), pCPUData(nullptr), prevFlashedEnd(0) {}

    bool Init(ID3D12Device* pDevice, D3D12MA::Allocator* pMemAllocator, D3D12_QUERY_HEAP_TYPE heapType, UINT size)
    {
        HRESULT hr = S_OK;

        heapSize = size;

        // Create query heap
        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = heapType;
        heapDesc.Count = (UINT)heapSize;
        heapDesc.NodeMask = 0;

        D3D_CHECK(pDevice->CreateQueryHeap(&heapDesc, __uuidof(ID3D12QueryHeap), (void**)&pGPUHeap));

        if (SUCCEEDED(hr))
        {
            D3D12MA::ALLOCATION_DESC allocDesc;
            allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
            allocDesc.CustomPool = nullptr;
            allocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_NONE;
            allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;

            D3D_CHECK(pMemAllocator->CreateResource(&allocDesc, &CD3DX12_RESOURCE_DESC::Buffer(heapSize * sizeof(UINT64)), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &cpuBuffer.pAllocation, __uuidof(ID3D12Resource), (void**)&cpuBuffer.pResource));
        }

        if (SUCCEEDED(hr))
        {
            D3D12_RANGE range;
            range.Begin = 0;
            range.End = heapSize * sizeof(64);
            D3D_CHECK(cpuBuffer.pResource->Map(0, &range, (void**)&pCPUData));
        }

        if (SUCCEEDED(hr))
        {
            callbacks.resize(heapSize);

            return RingBuffer<QueryRingBuffer, std::function<void(UINT64)>>::Init(heapSize);
        }

        return false;
    }

    void Term()
    {
        if (pCPUData != nullptr)
        {
            assert(cpuBuffer.pResource != nullptr);

            cpuBuffer.pResource->Unmap(0, nullptr);

            pCPUData = nullptr;
        }

        D3D_RELEASE(cpuBuffer.pResource);
        D3D_RELEASE(cpuBuffer.pAllocation);

        D3D_RELEASE(pGPUHeap);
    }

    inline std::function<void(UINT64)>& At(UINT64 idx) { return callbacks[idx]; }

    void AddPendingFence(UINT64 fenceValue)
    {
        if (!IsEmpty())
        {
            prevAllocEnd = allocEnd;

            RingBuffer<QueryRingBuffer, std::function<void(UINT64)>>::AddPendingFence(fenceValue);
        }
    }

    void FlashFenceValue(UINT64 fenceValue)
    {
        while (!pendingFences.empty() && pendingFences.front().fenceValue <= fenceValue)
        {
            UINT64 flashedValue = pendingFences.front().allocEnd;
            if (prevFlashedEnd < flashedValue)
            {
                for (UINT64 i = prevFlashedEnd; i < flashedValue; i++)
                {
                    if (callbacks[i] != nullptr)
                    {
                        callbacks[i](pCPUData[i]);
                        callbacks[i] = std::function<void(UINT64)>();
                    }
                }
            }
            else
            {
                for (UINT64 i = prevFlashedEnd; i < allocMaxSize; i++)
                {
                    if (callbacks[i] != nullptr)
                    {
                        callbacks[i](pCPUData[i]);
                        callbacks[i] = std::function<void(UINT64)>();
                    }
                }
                for (UINT64 i = 0; i < flashedValue; i++)
                {
                    if (callbacks[i] != nullptr)
                    {
                        callbacks[i](pCPUData[i]);
                        callbacks[i] = std::function<void(UINT64)>();
                    }
                }
            }

            prevFlashedEnd = flashedValue;

            allocStart = pendingFences.front().allocEnd;
            pendingFences.pop();
        }
    }

    HRESULT Resolve(ID3D12GraphicsCommandList* pCommandList)
    {
        if (!IsEmpty())
        {
            std::pair<UINT64, UINT64> range0, range1;

            GetResolveRanges(range0, range1);

            if (range0.second != 0)
            {
                pCommandList->ResolveQueryData(
                    GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, (UINT)range0.first, (UINT)range0.second, cpuBuffer.pResource, (UINT)range0.first * sizeof(UINT64)
                );
            }

            if (range1.second != 0)
            {
                pCommandList->ResolveQueryData(
                    GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, (UINT)range1.first, (UINT)range1.second, cpuBuffer.pResource, (UINT)range1.first * sizeof(UINT64)
                );
            }
        }

        return S_OK;
    }

    /** Get query heap */
    ID3D12QueryHeap* GetQueryHeap() const { return pGPUHeap; }

private:
    bool IsEmpty() const
    {
        return prevAllocEnd == allocEnd;
    }

    void GetResolveRanges(std::pair<UINT64, UINT64>& range0, std::pair<UINT64, UINT64>& range1) const
    {
        if (prevAllocEnd < allocEnd)
        {
            range0 = std::make_pair(prevAllocEnd, allocEnd - prevAllocEnd);
            range1 = std::make_pair(-1, 0);
        }
        else
        {
            range0 = std::make_pair(prevAllocEnd, allocMaxSize - prevAllocEnd);
            range1 = std::make_pair(0, allocEnd);
        }
    }

private:
    ID3D12QueryHeap* pGPUHeap;
    UINT64 prevAllocEnd;
    UINT64 prevFlashedEnd;

    GPUResource cpuBuffer;
    UINT64* pCPUData;
    UINT64 heapSize;

    std::vector<std::function<void(UINT64)>> callbacks;
};

DeviceTimeQuery::DeviceTimeQuery(Device* pDevice)
    : m_startTicks(0)
    , m_endTicks(0)
    , m_pDevice(pDevice)
    , m_freq(0)
{
    if (pDevice != nullptr)
    {
        m_freq = pDevice->GetPresentQueueFrequency();
    }
}

void DeviceTimeQuery::Start(ID3D12GraphicsCommandList* pCmdList)
{
    assert(m_pDevice != nullptr);
    m_pDevice->QueryTimestamp(pCmdList, [&start = m_startTicks](UINT64 value) {start = value; });
}

void DeviceTimeQuery::Stop(ID3D12GraphicsCommandList* pCmdList)
{
    assert(m_pDevice != nullptr);
    m_pDevice->QueryTimestamp(pCmdList, [&end = m_endTicks](UINT64 value) {end = value; });
}

/** Get value in milliseconds */
double DeviceTimeQuery::GetMSec() const
{
    return (double)(m_endTicks - m_startTicks) / m_freq * 1000.0;
}

/** Get value in milliseconds */
double DeviceTimeQuery::GetUSec() const
{
    return (double)(m_endTicks - m_startTicks) / m_freq * 1000000.0;
}

Device::Device()
    : m_pFactory(nullptr)
    , m_pAdapter(nullptr)
    , m_pDevice(nullptr)
    , m_pSwapchain(nullptr)
    , m_pBackBufferViews(nullptr)
    , m_pRenderTargetViews(nullptr)
    , m_maxRTViews(0)
    , m_currentRTView(0)
    , m_currentBackBufferIdx(0)
    , m_backBufferCount(0)
    , m_debugShaders(false)
    , m_debugDevice(false)
    , m_rtvDescSize(0)
    , m_dsvDescSize(0)
    , m_srvDescSize(0)
    , m_pGPUMemAllocator(nullptr)
    , m_pUploadBuffer(nullptr)
    , m_pReadbackBuffer(nullptr)
    , m_pPresentQueue(nullptr)
    , m_pUploadQueue(nullptr)
    , m_pUploadStateTransitionQueue(nullptr)
    , m_pCurrentUploadCmdList(nullptr)
    , m_pDynamicBuffer(nullptr)
    , m_pDynamicDescBuffer(nullptr)
    , m_dynamicDescCount(0)
    , m_isInitialized(false)
    , m_pDescriptorHeap(nullptr)
    , m_staticDescCount(0)
    , m_currentStaticDescIndex(0)
    , m_pQueryBuffer(nullptr)
{}

Device::~Device()
{
    assert(m_pUploadQueue == nullptr);
    assert(m_pUploadStateTransitionQueue == nullptr);
    assert(m_pUploadBuffer == nullptr);
    assert(m_pCurrentUploadCmdList == nullptr);

    assert(m_pDynamicBuffer == nullptr);
    assert(m_pDynamicDescBuffer == nullptr);

    assert(m_pReadbackBuffer == nullptr);

    assert(m_pSwapchain == nullptr);
    assert(m_pBackBufferViews == nullptr);
    assert(m_pRenderTargetViews == nullptr);
    assert(m_maxRTViews == 0);
    assert(m_currentRTView == 0);

    assert(m_pDevice == nullptr);
    assert(m_pAdapter == nullptr);
    assert(m_pFactory == nullptr);

    assert(m_pGPUMemAllocator == nullptr);

    assert(m_pPresentQueue == nullptr);

    assert(!m_isInitialized);

    assert(m_pDescriptorHeap == nullptr);

    assert(m_pQueryBuffer == nullptr);
}

bool Device::Create(const DeviceCreateParams& params)
{
    bool res = InitD3D12Device(params.debugLayer);
    if (res)
    {
        res = InitCommandQueue(params.cmdListCount, params.uploadListCount);
    }
    if (res)
    {
        res = InitSwapchain(params.cmdListCount, params.hWnd);
    }
    if (res)
    {
        res = InitGPUMemoryAllocator();
    }
    if (res)
    {
        res = InitDescriptorHeap(params.staticDescCount + params.dynamicDescCount);
    }
    if (res)
    {
        res = InitUploadEngine(params.uploadHeapSizeMb * 1024 * 1024, params.dynamicHeapSizeMb * 1024 * 1024, params.dynamicDescCount, m_pDescriptorHeap);
    }
    if (res)
    {
        res = InitRenderTargetViewHeap(params.renderTargetViewCount);
    }
    if (res)
    {
        res = InitReadbackEngine(params.readbackHeapSizeMb * 1024 * 1024);
    }
    if (res)
    {
        res = InitQueries(params.queryCount);
    }

    if (res)
    {
        m_pPresentQueue->SetSwapchain(m_pSwapchain);

        m_debugShaders = params.debugShaders;

        m_staticDescCount = params.staticDescCount;
    }

    if (!res)
    {
        Destroy();
    }
    else
    {
        m_isInitialized = true;
    }

    return res;
}

void Device::Destroy()
{
    WaitGPUIdle();

    m_rtvDescSize = 0;
    m_srvDescSize = 0;
    m_dsvDescSize = 0;

    TermQueries();
    TermReadbackEngine();
    TermRenderTargetViewHeap();
    TermUploadEngine();
    TermDescriptorHeap();
    TermGPUMemoryAllocator();
    TermSwapchain();
    TermCommandQueue();
    TermD3D12Device();

    m_isInitialized = false;
}

bool Device::BeginRenderCommandList(ID3D12GraphicsCommandList** ppCommandList, ID3D12Resource** ppBackBuffer, D3D12_CPU_DESCRIPTOR_HANDLE* pBackBufferDesc)
{
    m_currentBackBufferIdx = (m_currentBackBufferIdx + 1) % m_backBufferCount;

    UINT64 finishedFenceValue = NoneValue;

    HRESULT hr = m_pPresentQueue->OpenCommandList(ppCommandList, finishedFenceValue);
    if (finishedFenceValue != NoneValue)
    {
        m_pDynamicBuffer->FlashFenceValue(finishedFenceValue);
        m_pDynamicDescBuffer->FlashFenceValue(finishedFenceValue);
        m_pQueryBuffer->FlashFenceValue(finishedFenceValue);
    }
    if (SUCCEEDED(hr))
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pBackBufferViews->GetCPUDescriptorHandleForHeapStart());
        rtvHandle.ptr += m_rtvDescSize * m_currentBackBufferIdx;
        *pBackBufferDesc = rtvHandle;
        *ppBackBuffer = m_backBuffers[m_currentBackBufferIdx];
    }

    return SUCCEEDED(hr);
}

bool Device::CloseSubmitAndPresentRenderCommandList(bool vsync)
{
    // Close command list
    HRESULT hr = S_OK;

    // Submit update command list, if needed
    UINT64 uploadFenceValue = NoneValue;
    D3D_CHECK(m_pUploadQueue->SubmitCommandList(&uploadFenceValue));

    if (uploadFenceValue != NoneValue)
    {
        m_pUploadBuffer->AddPendingFence(uploadFenceValue);
        if (!m_uploadBarriers.empty())
        {
            ID3D12GraphicsCommandList* pBarrierCmdList = nullptr;
            D3D_CHECK(m_pUploadStateTransitionQueue->GetQueue()->Wait(m_pUploadQueue->GetCurrentCommandList()->GetFence(), uploadFenceValue));

            UINT64 finishedFenceValue = NoneValue;
            D3D_CHECK(m_pUploadStateTransitionQueue->OpenCommandList(&pBarrierCmdList, finishedFenceValue));

            for (auto barrier : m_uploadBarriers)
            {
                if (barrier.stateAfter != D3D12_RESOURCE_STATE_COMMON)
                {
                    TransitResourceState(pBarrierCmdList, barrier.pResource, D3D12_RESOURCE_STATE_COMMON, barrier.stateAfter);
                }
            }
            m_uploadBarriers.clear();

            D3D_CHECK(m_pUploadStateTransitionQueue->CloseAndSubmitCommandList(&uploadFenceValue));

            D3D_CHECK(m_pPresentQueue->GetQueue()->Wait(m_pUploadStateTransitionQueue->GetCurrentCommandList()->GetFence(), uploadFenceValue));
        }
        else
        {
            D3D_CHECK(m_pPresentQueue->GetQueue()->Wait(m_pUploadQueue->GetCurrentCommandList()->GetFence(), uploadFenceValue));
        }
    }

    m_pPresentQueue->SetVSync(vsync);

    D3D_CHECK(m_pQueryBuffer->Resolve(m_pPresentQueue->GetCurrentCommandList()->GetGraphicsCommandList()));

    UINT64 presentFenceValue = NoneValue;
    D3D_CHECK(m_pPresentQueue->CloseAndSubmitCommandList(&presentFenceValue));
    if (SUCCEEDED(hr))
    {
        m_pDynamicBuffer->AddPendingFence(presentFenceValue);
        m_pDynamicDescBuffer->AddPendingFence(presentFenceValue);
        m_pQueryBuffer->AddPendingFence(presentFenceValue);
    }

    return SUCCEEDED(hr);
}

bool Device::BeginUploadCommandList(ID3D12GraphicsCommandList** ppCommandList)
{
    // Wait for previous commit completion
    UINT64 finishedFenceValue = NoneValue;
    HRESULT hr = S_OK;
    D3D_CHECK(m_pUploadQueue->OpenCommandList(ppCommandList, finishedFenceValue));
    if (finishedFenceValue != NoneValue)
    {
        m_pUploadBuffer->FlashFenceValue(finishedFenceValue);
    }

    m_pCurrentUploadCmdList = *ppCommandList;

    return SUCCEEDED(hr);
}

HRESULT Device::UpdateBuffer(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pBuffer, const void* pData, size_t dataSize)
{
#ifdef _DEBUG
    D3D12_RESOURCE_DESC desc = pBuffer->GetDesc();
    assert(desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(desc.Width >= dataSize);
#endif

    assert(m_pCurrentUploadCmdList == pCommandList);

    UINT64 allocStartOffset = 0;
    UINT8* pAlloc = nullptr;
    auto allocRes = m_pUploadBuffer->Alloc(dataSize, allocStartOffset, pAlloc, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    assert(allocRes == RingBufferResult::Ok);
    if (allocRes == RingBufferResult::Ok)
    {
        memcpy(pAlloc, pData, dataSize);

        pCommandList->CopyBufferRegion(pBuffer, 0, m_pUploadBuffer->GetBuffer(), allocStartOffset, dataSize);

        return S_OK;
    }

    return E_FAIL;
}

HRESULT Device::UpdateTexture(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pTexture, const void* pData, size_t dataSize)
{
    D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
    assert(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

    UINT64 total = 0;
    std::vector<UINT> numRows(desc.MipLevels);
    std::vector<UINT64> rowSize(desc.MipLevels);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> placedFootprint(desc.MipLevels);

    m_pDevice->GetCopyableFootprints(&desc, 0, desc.MipLevels, 0, placedFootprint.data(), numRows.data(), rowSize.data(), &total);

    assert(m_pCurrentUploadCmdList == pCommandList);

    UINT64 width = desc.Width;
    UINT64 height = desc.Height;
    const UINT8* pSrcData = static_cast<const UINT8*>(pData);

    UINT64 allocStartOffset = 0;

    UINT8* pAlloc = nullptr;
    auto allocRes = m_pUploadBuffer->Alloc(total, allocStartOffset, pAlloc, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    assert(allocRes == RingBufferResult::Ok);
    if (allocRes != RingBufferResult::Ok)
    {
        return E_FAIL;
    }

    UINT pixelSize = 0;
    switch (desc.Format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            pixelSize = 4;
            break;

        case DXGI_FORMAT_A8_UNORM:
            pixelSize = 1;
            break;

        case DXGI_FORMAT_R32G32B32_FLOAT:
            pixelSize = 12;
            break;

        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            pixelSize = 16;
            break;

        default:
            assert(0); // Unknown format
            break;
    }

    UINT8* pDstData = static_cast<UINT8*>(pAlloc);

    for (int i = 0; i < desc.MipLevels; i++)
    {
        // Copy from pData
        for (UINT j = 0; j < height; j++)
        {
            memcpy(pDstData, pSrcData, width * pixelSize);
            pDstData += placedFootprint[i].Footprint.RowPitch;
            pSrcData += width * pixelSize;
        }

        placedFootprint[i].Offset += allocStartOffset;

        pCommandList->CopyTextureRegion(
            &CD3DX12_TEXTURE_COPY_LOCATION(pTexture, i),
            0, 0, 0,
            &CD3DX12_TEXTURE_COPY_LOCATION(m_pUploadBuffer->GetBuffer(), placedFootprint[i]),
            nullptr);

        width /= 2;
        height /= 2;
    }

    return S_OK;
}

void Device::CloseUploadCommandList()
{
    m_pUploadQueue->CloseCommandList();
    m_pCurrentUploadCmdList = nullptr;
}

bool Device::TransitResourceState(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
{
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = pResource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;

    pCommandList->ResourceBarrier(1, &barrier);

    return true;
}

bool Device::CreateGPUResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialResourceState,
    const D3D12_CLEAR_VALUE *pOptimizedClearValue, GPUResource& resource, const void* pInitialData, size_t initialDataSize)
{
    D3D12MA::ALLOCATION_DESC allocDesc;
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    allocDesc.CustomPool = nullptr;
    allocDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_NONE;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;

    HRESULT hr = S_OK;
    if (pInitialData == nullptr)
    {
        D3D_CHECK(m_pGPUMemAllocator->CreateResource(&allocDesc, &desc, initialResourceState, pOptimizedClearValue, &resource.pAllocation, __uuidof(ID3D12Resource), (void**)&resource.pResource));
    }
    else
    {
        assert(m_pCurrentUploadCmdList != nullptr);

        D3D_CHECK(m_pGPUMemAllocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON, pOptimizedClearValue, &resource.pAllocation, __uuidof(ID3D12Resource), (void**)&resource.pResource));
        switch (desc.Dimension)
        {
            case D3D12_RESOURCE_DIMENSION_BUFFER:
                D3D_CHECK(UpdateBuffer(m_pCurrentUploadCmdList, resource.pResource, pInitialData, initialDataSize));
                break;

            case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
                D3D_CHECK(UpdateTexture(m_pCurrentUploadCmdList, resource.pResource, pInitialData, initialDataSize));
                break;
        }
        
        if (SUCCEEDED(hr) && initialResourceState != D3D12_RESOURCE_STATE_COMMON)
        {
            m_uploadBarriers.push_back({resource.pResource, initialResourceState});
        }
    }

    return SUCCEEDED(hr);
}

void Device::ReleaseGPUResource(GPUResource& resource)
{
    D3D_RELEASE(resource.pResource);
    D3D_RELEASE(resource.pAllocation);
}

bool Device::CompileShader(LPCTSTR srcFilename, const std::vector<LPCSTR>& defines, const ShaderStage& stage, ID3DBlob** ppShaderBinary, std::set<std::tstring>* pIncludes)
{
    std::vector<char> data;

    // AAV TEMP
    if (srcFilename == L"Material.hlsl")
    {
        int h = 0;
    }

    bool res = ReadFileContent(srcFilename, data);
    if (res)
    {
        std::vector<D3D_SHADER_MACRO> macros;
        for (int i = 0; i < defines.size(); i++)
        {
            macros.push_back({defines[i], nullptr});
        }
        macros.push_back({ nullptr, nullptr });

        UINT flags = 0;
        if (m_debugShaders)
        {
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
        }

        ID3DBlob* pErrMsg = nullptr;

        HRESULT hr = S_OK;

        D3DInclude includeCallback;

        switch (stage)
        {
            case Vertex:
                hr = D3DCompile(data.data(), data.size(), "", macros.data(), &includeCallback, "VS", "vs_5_0", flags, 0, ppShaderBinary, &pErrMsg);
                break;

            case Pixel:
                hr = D3DCompile(data.data(), data.size(), "", macros.data(), &includeCallback, "PS", "ps_5_0", flags, 0, ppShaderBinary, &pErrMsg);
                break;

            case Compute:
                hr = D3DCompile(data.data(), data.size(), "", macros.data(), &includeCallback, "CS", "cs_5_0", flags, 0, ppShaderBinary, &pErrMsg);
                break;
        }
        if (pErrMsg != nullptr)
        {
            if (!SUCCEEDED(hr))
            {
                const char* pMsg = (const char*)pErrMsg->GetBufferPointer();
                OutputDebugStringA(pMsg);
                OutputDebugString(_T("\n"));
            }
            D3D_RELEASE(pErrMsg);
        }
        assert(SUCCEEDED(hr));

        res = SUCCEEDED(hr);

        if (res && pIncludes != nullptr)
        {
            std::swap(*pIncludes, includeCallback.includeFiles);
        }
    }
    return res;
}

bool Device::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& rsDesc, ID3D12RootSignature** ppRootSignature)
{
    ID3DBlob* pSignatureBlob = nullptr;
    ID3DBlob* pErrMsg = nullptr;

    HRESULT hr = S_OK;
    D3D_CHECK(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSignatureBlob, &pErrMsg));
    if (pErrMsg != nullptr)
    {
        if (!SUCCEEDED(hr))
        {
            const char* pMsg = (const char*)pErrMsg->GetBufferPointer();
            OutputDebugStringA(pMsg);
            OutputDebugString(_T("\n"));
        }
        D3D_RELEASE(pErrMsg);
    }

    D3D_CHECK(m_pDevice->CreateRootSignature(0, pSignatureBlob->GetBufferPointer(), pSignatureBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)ppRootSignature));

    D3D_RELEASE(pSignatureBlob);

    return SUCCEEDED(hr);
}

bool Device::CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, ID3D12PipelineState** ppPSO)
{
    HRESULT hr = S_OK;
    m_pDevice->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)ppPSO);
    return SUCCEEDED(hr);
}

bool Device::ResizeSwapchain(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }

    WaitGPUIdle();

    HRESULT hr = S_OK;
    DXGI_SWAP_CHAIN_DESC desc;
    D3D_CHECK(m_pSwapchain->GetDesc(&desc));
    if (desc.BufferDesc.Width != width || desc.BufferDesc.Height != height)
    {
        UINT count = (UINT)m_backBuffers.size();

        TermBackBuffers();

        D3D_CHECK(m_pSwapchain->ResizeBuffers(count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        D3D_CHECK(CreateBackBuffers(count));
    }

    return SUCCEEDED(hr);
}

bool Device::AllocateDynamicBuffers(UINT count, const UINT* pSizes, D3D12_GPU_DESCRIPTOR_HANDLE& startHandle, UINT8** ppCPUData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDescs)
{
    UINT64 allocStartOffset = 0;

    // TODO Should we use additional wait here? For the frame that is already submitted and not ready on GPU yet
    // Or just leave an error state
    RingBufferResult res = m_pDynamicDescBuffer->Alloc(count, allocStartOffset, startHandle, 1);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pDynamicDescBuffer->AtCPU(allocStartOffset);
    if (res == RingBufferResult::Ok)
    {
        return AllocateDynamicBuffers(count, pSizes, handle, ppCPUData, pDescs);
    }
    return false;
}

bool Device::AllocateDynamicBuffers(UINT count, const UINT* pSizes, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, UINT8** ppCPUData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDescs)
{
    UINT64 allocStartOffset = 0;

    RingBufferResult res = RingBufferResult::Ok;

    for (UINT i = 0; i < count && res == RingBufferResult::Ok; i++)
    {
        UINT alignedSize = Align(pSizes[i], (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        res = m_pDynamicBuffer->Alloc(alignedSize, allocStartOffset, ppCPUData[i], D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (res == RingBufferResult::Ok)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
            desc.BufferLocation = m_pDynamicBuffer->GetBuffer()->GetGPUVirtualAddress() + allocStartOffset;
            desc.SizeInBytes = (UINT)alignedSize;

            m_pDevice->CreateConstantBufferView(&desc, cpuHandle);

            if (pDescs != nullptr)
            {
                pDescs[i] = desc;
            }

            cpuHandle.ptr += m_rtvDescSize;
        }
    }

    return res == RingBufferResult::Ok;
}

bool Device::AllocateDynamicBuffer(UINT size, UINT alignment, void** ppCPUData, UINT64& gpuVirtualAddress)
{
    UINT alignedSize = Align(size, (UINT)alignment);

    UINT64 allocStartOffset = 0;
    RingBufferResult res = m_pDynamicBuffer->Alloc(alignedSize, allocStartOffset, *((UINT8**)ppCPUData), alignment);
    if (res == RingBufferResult::Ok)
    {
        gpuVirtualAddress = m_pDynamicBuffer->GetBuffer()->GetGPUVirtualAddress() + allocStartOffset;
    }

    return res == RingBufferResult::Ok;
}

ID3D12DescriptorHeap* Device::GetDescriptorHeap() const
{
    return m_pDescriptorHeap;
}

bool Device::AllocateRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, UINT count)
{
    assert(m_currentRTView < m_maxRTViews && m_pRenderTargetViews != nullptr);

    if (m_currentRTView < m_maxRTViews)
    {
        cpuHandle = m_pRenderTargetViews->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += m_rtvDescSize * m_currentRTView;

        m_currentRTView += count;

        return true;
    }

    return false;
}

bool Device::AllocateStaticDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpuStartHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuStartHandle)
{
    assert(m_currentStaticDescIndex + count <= m_staticDescCount);

    cpuStartHandle = m_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    cpuStartHandle.ptr += (m_currentStaticDescIndex + m_dynamicDescCount) * m_srvDescSize;

    gpuStartHandle = m_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    gpuStartHandle.ptr += (m_currentStaticDescIndex + m_dynamicDescCount) * m_srvDescSize;

    m_currentStaticDescIndex += count;

    return true;
}

bool Device::AllocateDynamicDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpuStartHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuStartHandle)
{
    UINT64 allocStartOffset = 0;

    // TODO Should we use additional wait here? For the frame that is already submitted and not ready on GPU yet
    // Or just leave an error state
    RingBufferResult res = m_pDynamicDescBuffer->Alloc(count, allocStartOffset, gpuStartHandle, 1);
    if (res == RingBufferResult::Ok)
    {
        cpuStartHandle = m_pDynamicDescBuffer->AtCPU(allocStartOffset);
    }
    return res == RingBufferResult::Ok;
}

bool Device::InitD3D12Device(bool debugDevice)
{
    m_pFactory = nullptr;

    m_debugDevice = debugDevice;

    bool strictDebug = false;

    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&m_pFactory);
    assert(SUCCEEDED(hr));

    if (debugDevice)
    {
        ID3D12Debug1* pDebug = nullptr;
        HRESULT hr = D3D12GetDebugInterface(__uuidof(ID3D12Debug1), (void**)&pDebug);
        assert(SUCCEEDED(hr));

        pDebug->EnableDebugLayer();
        if (strictDebug)
        {
            pDebug->SetEnableGPUBasedValidation(TRUE);
        }

        pDebug->Release();
    }

    // Find suitable GPU adapter
    UINT adapterIdx = 0;
    IDXGIAdapter* pAdapter = nullptr;
    while (m_pFactory->EnumAdapters(adapterIdx, &pAdapter) == S_OK)
    {
        bool skip = false;

        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);

        skip = wcscmp(desc.Description, L"Microsoft Basic Render Driver") == 0;
        if (!skip)
        {
            skip = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&m_pDevice) != S_OK;
            // Fall back to 11_1 feature level, if 12_0 is not supported
            if (skip)
            {
                OutputDebugString(_T("Feature level 12_0 is not supported on "));
                OutputDebugStringW(desc.Description);
                OutputDebugString(_T(", fall back to 11_1\n"));
                skip = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_1, __uuidof(ID3D12Device), (void**)&m_pDevice) != S_OK;
            }
        }

        if (!skip)
        {
            m_pAdapter = pAdapter;
            break;
        }

        pAdapter->Release();

        adapterIdx++;
    }

    if (m_pAdapter != nullptr)
    {
        IDXGIOutput* pOutput = nullptr;
        int outputIndex = 0;
        while (m_pAdapter->EnumOutputs(outputIndex, &pOutput) == S_OK)
        {
            DXGI_OUTPUT_DESC desc;
            pOutput->GetDesc(&desc);

            pOutput->Release();

            ++outputIndex;
        }
    }

    if (m_pDevice != nullptr)
    {
        m_srvDescSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_dsvDescSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_rtvDescSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    if (m_pDevice != nullptr && debugDevice)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;

        HRESULT hr = S_OK;

        D3D_CHECK(m_pDevice->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&pInfoQueue));
        if (strictDebug)
        {
            D3D_CHECK(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
        }
        D3D_CHECK(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
        D3D_CHECK(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
        D3D_RELEASE(pInfoQueue);
    }

    return m_pDevice != nullptr;
}

void Device::TermD3D12Device()
{
    ULONG refs = 0;
    if (m_pDevice != nullptr)
    {
        refs = m_pDevice->Release();
        m_pDevice = nullptr;
    }

    if (m_debugDevice && refs)
    {
        IDXGIDebug1* pDxgiDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, __uuidof(IDXGIDebug1), (void**)&pDxgiDebug)))
        {
            pDxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL));
            D3D_RELEASE(pDxgiDebug);
        }
    }

    D3D_RELEASE(m_pAdapter);
    D3D_RELEASE(m_pFactory);
}

bool Device::InitCommandQueue(int count, int uploadCount)
{
    m_pPresentQueue = new PresentCommandQueue();
    bool res = m_pPresentQueue->Init(m_pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, count);
    if (res)
    {
        m_pUploadQueue = new UploadCommandQueue();
        res = m_pUploadQueue->Init(m_pDevice, D3D12_COMMAND_LIST_TYPE_COPY, uploadCount);
    }
    if (res)
    {
        m_pUploadStateTransitionQueue = new CommandQueue();
        res = m_pUploadStateTransitionQueue->Init(m_pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, count);
    }
    return res;
}

void Device::TermCommandQueue()
{
    RELEASE(m_pUploadStateTransitionQueue);
    RELEASE(m_pUploadQueue);
    RELEASE(m_pPresentQueue);
}

bool Device::InitSwapchain(int count, HWND hWnd)
{
    DXGI_MODE_DESC bufferDesc;
    bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bufferDesc.Height = 0;
    bufferDesc.Width = 0;
    bufferDesc.RefreshRate.Numerator = 0;
    bufferDesc.RefreshRate.Denominator = 1;
    bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    DXGI_SWAP_CHAIN_DESC desc;
    desc.BufferCount = count;
    desc.BufferDesc = bufferDesc;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hWnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    IDXGISwapChain* pSwapchain = nullptr;

    HRESULT hr = S_OK;
    D3D_CHECK(m_pFactory->CreateSwapChain(m_pPresentQueue->GetQueue(), &desc, &pSwapchain));
    D3D_CHECK(pSwapchain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_pSwapchain));
    if (hr != S_OK)
    {
        OutputDebugString(_T("DXGI Swapchain version 1.4 is not supported\n"));
    }
    D3D_RELEASE(pSwapchain);

    if (SUCCEEDED(hr))
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = count;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 0;

        D3D_CHECK(m_pDevice->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), (void**)&m_pBackBufferViews));
    }

    D3D_CHECK(CreateBackBuffers(count));

    return SUCCEEDED(hr);
}

void Device::TermSwapchain()
{
    D3D_RELEASE(m_pBackBufferViews);

    TermBackBuffers();

    D3D_RELEASE(m_pSwapchain);
}

bool Device::InitGPUMemoryAllocator()
{
    D3D12MA::ALLOCATOR_DESC desc;
    desc.pAdapter = m_pAdapter;
    desc.pDevice = m_pDevice;
    desc.pAllocationCallbacks = nullptr;
    desc.PreferredBlockSize = 0;
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED;

    HRESULT hr = S_OK;
    D3D_CHECK(D3D12MA::CreateAllocator(&desc, &m_pGPUMemAllocator));

    return SUCCEEDED(hr);
}

void Device::TermGPUMemoryAllocator()
{
    D3D_RELEASE(m_pGPUMemAllocator);
}

bool Device::InitDescriptorHeap(UINT descCount)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;
    heapDesc.NumDescriptors = descCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    HRESULT hr = S_OK;
    D3D_CHECK(m_pDevice->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_pDescriptorHeap));

    return SUCCEEDED(hr);
}

void Device::TermDescriptorHeap()
{
    D3D_RELEASE(m_pDescriptorHeap);
}

bool Device::InitUploadEngine(UINT64 uploadHeapSize, UINT64 dynamicHeapSize, UINT dynamicDescCount, ID3D12DescriptorHeap* pDescHeap)
{
    bool res = true;
    if (res)
    {
        m_pUploadBuffer = new HeapRingBuffer();
        res = m_pUploadBuffer->Init(m_pDevice, D3D12_HEAP_TYPE_UPLOAD, uploadHeapSize);
    }
    if (res)
    {
        m_pDynamicBuffer = new HeapRingBuffer();
        res = m_pDynamicBuffer->Init(m_pDevice, D3D12_HEAP_TYPE_UPLOAD, dynamicHeapSize);
    }
    if (res)
    {
        m_pDynamicDescBuffer = new DescriptorRingBuffer();
        res = m_pDynamicDescBuffer->Init(m_pDevice, dynamicDescCount, pDescHeap);
    }
    if (res)
    {
        m_dynamicDescCount = dynamicDescCount;
    }
    return res;
}

void Device::TermUploadEngine()
{
    RELEASE(m_pDynamicDescBuffer);
    RELEASE(m_pDynamicBuffer);
    RELEASE(m_pUploadBuffer);
}

bool Device::InitReadbackEngine(UINT64 readbackHeapSize)
{
    bool res = true;
    if (res)
    {
        m_pReadbackBuffer = new HeapRingBuffer();
        res = m_pReadbackBuffer->Init(m_pDevice, D3D12_HEAP_TYPE_READBACK, readbackHeapSize);
    }

    return res;
}

void Device::TermReadbackEngine()
{
    RELEASE(m_pReadbackBuffer);
}

bool Device::InitRenderTargetViewHeap(UINT count)
{
    HRESULT hr = S_OK;
    if (SUCCEEDED(hr))
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = count;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 0;

        D3D_CHECK(m_pDevice->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), (void**)&m_pRenderTargetViews));
    }
    if (SUCCEEDED(hr))
    {
        m_maxRTViews = count;
        m_currentRTView = 0;
    }

    return SUCCEEDED(hr);
}

void Device::TermRenderTargetViewHeap()
{
    m_maxRTViews = 0;
    m_currentRTView = 0;

    D3D_RELEASE(m_pRenderTargetViews);
}

bool Device::InitQueries(UINT count)
{
    m_pQueryBuffer = new QueryRingBuffer();
    if (m_pQueryBuffer->Init(m_pDevice, m_pGPUMemAllocator, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, count))
    {
        return true;
    }

    RELEASE(m_pQueryBuffer);

    return false;
}

void Device::TermQueries()
{
    RELEASE(m_pQueryBuffer);
}

void Device::WaitGPUIdle()
{
    UINT64 finishedFenceValue = NoneValue;
    m_pPresentQueue->WaitIdle(finishedFenceValue);
    if (finishedFenceValue != NoneValue)
    {
        m_pDynamicBuffer->FlashFenceValue(finishedFenceValue);
        m_pDynamicDescBuffer->FlashFenceValue(finishedFenceValue);
    }
}

size_t Device::GetFramesInFlight()
{
    return m_pPresentQueue->GetCommandListCount();
}

bool Device::QueryTimestamp(ID3D12GraphicsCommandList* pCommandList, const std::function<void(UINT64)>& cb)
{
    UINT64 id = -1;
    UINT query = -1;
    std::function<void(UINT64)> queryCB;
    RingBufferResult allocRes = m_pQueryBuffer->Alloc(1, id, queryCB, 1);
    assert(allocRes == RingBufferResult::Ok);
    if (allocRes == RingBufferResult::Ok)
    {
        m_pQueryBuffer->At(id) = cb;

        pCommandList->EndQuery(m_pQueryBuffer->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, (UINT)id);

        return true;
    }

    return false;
}

UINT64 Device::GetPresentQueueFrequency() const
{
    UINT64 freq = 0;
    HRESULT hr = S_OK;
    D3D_CHECK(m_pPresentQueue->GetQueue()->GetTimestampFrequency(&freq));

    return freq;
}

HRESULT Device::CreateBackBuffers(UINT count)
{
    HRESULT hr = S_OK;

    for (UINT i = 0; i < count && SUCCEEDED(hr); i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        D3D_CHECK(m_pSwapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&pBackBuffer));
        m_backBuffers.push_back(pBackBuffer);
    }

    if (SUCCEEDED(hr))
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pBackBufferViews->GetCPUDescriptorHandleForHeapStart());

        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;

        for (UINT i = 0; i < count; i++)
        {
            m_pDevice->CreateRenderTargetView(m_backBuffers[i], &desc, rtvHandle);

            rtvHandle.ptr += m_rtvDescSize;
        }
    }

    if (SUCCEEDED(hr))
    {
        m_backBufferCount = count;

        // We actually should point at previous to grab current at new frame
        m_currentBackBufferIdx = (m_pSwapchain->GetCurrentBackBufferIndex() + m_backBufferCount - 1) % m_backBufferCount;
    }

    return hr;
}

void Device::TermBackBuffers()
{
    for (auto pBackBuffer : m_backBuffers)
    {
        D3D_RELEASE(pBackBuffer);
    }
    m_backBuffers.clear();

    m_backBufferCount = 0;
    m_currentBackBufferIdx = 0;
}

} // Platform
