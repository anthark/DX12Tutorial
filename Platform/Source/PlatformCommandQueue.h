#pragma once

#include <functional>

namespace Platform
{

const UINT64 NoneValue = (UINT64)-1;

class CommandList
{
public:
    bool Init(ID3D12Device* pDevice, const D3D12_COMMAND_LIST_TYPE& type);
    void Term();

    HRESULT Open(UINT64 fenceValue);
    HRESULT Close();
    HRESULT Submit(ID3D12CommandQueue* pQueue);

    HRESULT Wait(UINT64& finishedFenceValue);

    inline ID3D12GraphicsCommandList* GetGraphicsCommandList() const { return m_pCommandList; }
    inline ID3D12Fence* GetFence() const { return m_pFence; }
    inline HANDLE GetEvent() const { return m_hEvent; }
    inline UINT64 GetSubmittedFenceValue() const { return m_submittedFenceValue; }
    inline UINT64 GetPendingFenceValue() const { return m_pendingFenceValue; }

    void SetGPUFrameCB(const std::vector<std::function<bool()>>& cb) { m_cb = cb; }

private:
    UINT64 m_currentFenceValue = NoneValue;
    UINT64 m_pendingFenceValue = NoneValue;
    UINT64 m_submittedFenceValue = NoneValue;

    ID3D12CommandAllocator* m_pCommandAllocator = nullptr;
    ID3D12GraphicsCommandList* m_pCommandList = nullptr;
    ID3D12CommandList* m_pCommandListForSubmit = nullptr;

    ID3D12Fence* m_pFence = nullptr;
    HANDLE m_hEvent = INVALID_HANDLE_VALUE;

    std::vector<std::function<bool()>> m_cb;
};

class CommandQueue
{
public:
    virtual ~CommandQueue() {}

    bool Init(ID3D12Device* pDevice, const D3D12_COMMAND_LIST_TYPE& type, int cmdListCount);
    void Term();

    HRESULT OpenCommandList(ID3D12GraphicsCommandList** ppList, UINT64& finishedFenceValue);
    HRESULT CloseAndSubmitCommandList(UINT64* pSubmitFenceValue = nullptr, const std::vector<std::function<bool()>>& cb = {});
    HRESULT CloseCommandList();
    HRESULT SubmitCommandList(UINT64* pSubmitFenceValue = nullptr, const std::vector<std::function<bool()>>& cb = {});

    void WaitIdle(UINT64& finishedFenceValue);

    inline ID3D12CommandQueue* GetQueue() const { return m_pQueue; }
    inline CommandList* GetCurrentCommandList() const { return m_cmdLists[m_curCmdList]; }
    inline size_t GetCommandListCount() const { return m_cmdLists.size(); }

protected:
    virtual HRESULT PreSignal() { return S_OK; }

private:
    ID3D12CommandQueue* m_pQueue = nullptr;

    UINT64 m_currentFenceValue = 0;

    int m_curCmdList = -1;
    std::vector<CommandList*> m_cmdLists;
};

class PresentCommandQueue : public CommandQueue
{
public:
    virtual ~PresentCommandQueue() {}

    inline void SetSwapchain(IDXGISwapChain3* pSwapchain) { m_pSwapchain = pSwapchain; }

    inline void SetVSync(bool vsync) { m_vsync = vsync; }

protected:
    virtual HRESULT PreSignal() override;

private:
    IDXGISwapChain3* m_pSwapchain = nullptr;
    bool m_vsync = true;
};

class UploadCommandQueue : public CommandQueue
{
public:
    virtual ~UploadCommandQueue() {}
};

} // Platform
