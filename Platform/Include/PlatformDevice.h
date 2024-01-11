#pragma once

#include <vector>
#include <set>
#include <functional>

struct IDXGISwapChain3;

namespace D3D12MA
{
class Allocator;
class Allocation;
}

namespace Platform
{

#ifdef PIX_MARKERS
#define PIX_MARKER_SCOPE(name) Platform::PixEvent __pix_marker##name(GetCurrentCommandList(), 0x0000ff00, _T(#name));
#define PIX_MARKER_SCOPE_STR(name, str) Platform::PixEvent __pix_marker##name(GetCurrentCommandList(), 0x0000ff00, str);
#define PIX_MARKER_CMDLIST_SCOPE_STR(commandList, name, str) Platform::PixEvent __pix_marker##name(commandList, 0x0000ff00, str);
#else
#define PIX_MARKER_SCOPE(name)
#define PIX_MARKER_SCOPE_STR(name, str)
#define PIX_MARKER_CMDLIST_SCOPE_STR(commandList, name, str)
#endif

class PLATFORM_API PixEvent
{
public:
    PixEvent(ID3D12GraphicsCommandList* pCmdList, UINT64 color, LPCTSTR name);
    ~PixEvent();

private:
    ID3D12GraphicsCommandList* m_pCmdList;
};

struct DeviceCreateParams
{
    bool debugLayer = false;
    bool debugShaders = false;
    int  cmdListCount = 2;
    int  uploadListCount = 2;
    HWND hWnd = nullptr;
    int  uploadHeapSizeMb = 16;
    int  dynamicHeapSizeMb = 16;
    int  readbackHeapSizeMb = 1;
    int  dynamicDescCount = 16384;
    int  staticDescCount = 16384;
    int  renderTargetViewCount = 256;
    int  queryCount = 128;
};

class CommandQueue;
class PresentCommandQueue;
class UploadCommandQueue;

struct HeapRingBuffer;
struct DescriptorRingBuffer;
struct QueryRingBuffer;

struct GPUResource
{
    ID3D12Resource* pResource = nullptr;
    D3D12MA::Allocation* pAllocation = nullptr;
};

class Device;

// TODO Move it to some more appropriate place
class PLATFORM_API DeviceTimeQuery
{
public:
    DeviceTimeQuery(Device* pDevice = nullptr);

    void Start(ID3D12GraphicsCommandList* pCmdList);
    void Stop(ID3D12GraphicsCommandList* pCmdList);

    // Get value in milliseconds
    double GetMSec() const;
    // Get value in microseconds
    double GetUSec() const;

private:
    UINT64 m_startTicks;
    UINT64 m_endTicks;
    UINT64 m_freq;

    Device* m_pDevice;
};

class PLATFORM_API Device
{
public:
    enum ShaderStage
    {
        Vertex = 0,
        Pixel,
        Compute
    };

public:
    Device();
    virtual ~Device();

    bool Create(const DeviceCreateParams& params);
    void Destroy();

    bool BeginRenderCommandList(ID3D12GraphicsCommandList** ppCommandList, ID3D12Resource** ppBackBuffer, D3D12_CPU_DESCRIPTOR_HANDLE* pBackBufferDesc);
    bool CloseSubmitAndPresentRenderCommandList(bool vsync = true);

    bool BeginUploadCommandList(ID3D12GraphicsCommandList** ppCommandList);
    HRESULT UpdateBuffer(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pBuffer, const void* pData, size_t dataSize);
    HRESULT UpdateTexture(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pTexture, const void* pData, size_t dataSize);
    void CloseUploadCommandList();

    bool TransitResourceState(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    bool CreateGPUResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, GPUResource& resource, const void* pInitialData = nullptr, size_t initialDataSize = 0);
    void ReleaseGPUResource(GPUResource& resource);

    bool CompileShader(LPCTSTR srcFilename, const std::vector<LPCSTR>& defines, const ShaderStage& stage, ID3DBlob** ppShaderBinary, std::set<std::tstring>* pIncludes = nullptr);
    bool CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& rsDesc, ID3D12RootSignature** ppRootSignature);
    bool CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, ID3D12PipelineState** ppPSO);

    bool ResizeSwapchain(UINT width, UINT height);

    bool AllocateDynamicBuffers(UINT count, const UINT* pSizes, D3D12_GPU_DESCRIPTOR_HANDLE& startHandle, UINT8** ppCPUData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDescs = nullptr);
    bool AllocateDynamicBuffers(UINT count, const UINT* pSizes, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, UINT8** ppCPUData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDescs = nullptr);
    bool AllocateDynamicBuffer(UINT size, UINT alignment, void** ppCPUData, UINT64& gpuVirtualAddress);
    bool AllocateReadbackBuffer(UINT size, UINT alignment, void** ppCPUData, ID3D12Resource** ppReadBackBuffer, UINT64& offset);
    bool AllocateStaticDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpuStartHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuStartHandle);
    bool AllocateDynamicDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpuStartHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuStartHandle);
    ID3D12DescriptorHeap* GetDescriptorHeap() const;

    bool AllocateRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, UINT count = 1);

    inline bool IsInitialized() const { return m_isInitialized; }
    inline ID3D12Device* GetDXDevice() const { return m_pDevice; }

    inline UINT GetSRVDescSize() const { return m_srvDescSize; }
    inline UINT GetDSVDescSize() const { return m_dsvDescSize; }
    inline UINT GetRTVDescSize() const { return m_rtvDescSize; }

    void WaitGPUIdle();

    size_t GetFramesInFlight();

    inline bool IsDebug() const { return m_debugDevice; }

    bool QueryTimestamp(ID3D12GraphicsCommandList* pCommandList, const std::function<void(UINT64)>& cb);
    UINT64 GetPresentQueueFrequency() const;

    void AddGPUFrameCB(const std::function<bool()>& function) { m_gpuFrameCB.push_back(function); }

private:

    struct PendingBarrier
    {
        ID3D12Resource* pResource = nullptr;
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_COMMON;
    };

private:
    bool InitD3D12Device(bool debugDevice);
    void TermD3D12Device();

    bool InitCommandQueue(int count, int uploadCount);
    void TermCommandQueue();

    bool InitSwapchain(int count, HWND hWnd);
    void TermSwapchain();

    bool InitGPUMemoryAllocator();
    void TermGPUMemoryAllocator();

    bool InitDescriptorHeap(UINT descCount);
    void TermDescriptorHeap();

    bool InitUploadEngine(UINT64 uploadHeapSize, UINT64 dynamicHeapSize, UINT dynamicDescCount, ID3D12DescriptorHeap* pDescHeap);
    void TermUploadEngine();

    bool InitReadbackEngine(UINT64 readbackHeapSize);
    void TermReadbackEngine();

    bool InitRenderTargetViewHeap(UINT count);
    void TermRenderTargetViewHeap();

    bool InitQueries(UINT count);
    void TermQueries();

    HRESULT CreateBackBuffers(UINT count);
    void TermBackBuffers();

private:
    D3D12MA::Allocator* m_pGPUMemAllocator;

    IDXGIFactory* m_pFactory;
    IDXGIAdapter* m_pAdapter;
    ID3D12Device* m_pDevice;

    ID3D12DescriptorHeap* m_pDescriptorHeap;

    bool m_debugShaders;
    bool m_debugDevice;

    bool m_isInitialized;

    PresentCommandQueue* m_pPresentQueue;

    IDXGISwapChain3* m_pSwapchain;

    std::vector<ID3D12Resource*> m_backBuffers;
    ID3D12DescriptorHeap* m_pBackBufferViews;

    ID3D12DescriptorHeap* m_pRenderTargetViews;
    UINT m_maxRTViews;
    UINT m_currentRTView;

    UINT m_currentBackBufferIdx;
    UINT m_backBufferCount;

    UINT m_rtvDescSize;
    UINT m_dsvDescSize;
    UINT m_srvDescSize;

    // Queries support
    // -->
    QueryRingBuffer* m_pQueryBuffer;
    // <--

    // Dynamic resources support
    // -->
    HeapRingBuffer* m_pDynamicBuffer;
    DescriptorRingBuffer* m_pDynamicDescBuffer;
    UINT m_dynamicDescCount;
    // <--

    // Upload engine support
    // -->
    UploadCommandQueue* m_pUploadQueue;
    CommandQueue* m_pUploadStateTransitionQueue;
    ID3D12GraphicsCommandList* m_pCurrentUploadCmdList;
    std::vector<PendingBarrier> m_uploadBarriers;

    HeapRingBuffer* m_pUploadBuffer;
    // <--

    // Readback engine support
    // -->
    HeapRingBuffer* m_pReadbackBuffer;
    // <--

    // Static descriptor heap
    // -->
    UINT m_staticDescCount;
    UINT m_currentStaticDescIndex;
    // <--

    std::vector<std::function<bool()>> m_gpuFrameCB;
};

} // Platform

