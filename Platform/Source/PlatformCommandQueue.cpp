#include "stdafx.h"
#include "PlatformCommandQueue.h"

#include "Platform.h"

namespace Platform
{

bool CommandList::Init(ID3D12Device* pDevice, const D3D12_COMMAND_LIST_TYPE& type)
{
    HRESULT hr = S_OK;
    D3D_CHECK(pDevice->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator), (void**)&m_pCommandAllocator));
    D3D_CHECK(pDevice->CreateCommandList(0, type, m_pCommandAllocator, nullptr, __uuidof(ID3D12CommandList), (void**)&m_pCommandListForSubmit));
    D3D_CHECK(m_pCommandListForSubmit->QueryInterface(__uuidof(ID3D12GraphicsCommandList), (void**)&m_pCommandList));
    D3D_CHECK(m_pCommandList->Close());
    D3D_CHECK(m_pCommandAllocator->Reset());
    D3D_CHECK(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_pFence));
    if (SUCCEEDED(hr))
    {
        m_hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hr = m_hEvent != INVALID_HANDLE_VALUE ? S_OK : E_FAIL;
    }

    if (!SUCCEEDED(hr))
    {
        Term();
    }

    return SUCCEEDED(hr);
}

void CommandList::Term()
{
    if (m_hEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hEvent);
        m_hEvent = INVALID_HANDLE_VALUE;
    }

    D3D_RELEASE(m_pFence);

    D3D_RELEASE(m_pCommandListForSubmit);
    D3D_RELEASE(m_pCommandList);
    D3D_RELEASE(m_pCommandAllocator);
}

HRESULT CommandList::Open(UINT64 fenceValue)
{
    assert(m_pendingFenceValue == NoneValue && m_submittedFenceValue == NoneValue && m_currentFenceValue == NoneValue);

    HRESULT hr = S_OK;
    D3D_CHECK(m_pCommandAllocator->Reset());
    D3D_CHECK(m_pCommandList->Reset(m_pCommandAllocator, nullptr));

    m_currentFenceValue = fenceValue;

    return hr;
}

HRESULT CommandList::Close()
{
    assert(m_pendingFenceValue == NoneValue && m_submittedFenceValue == NoneValue && m_currentFenceValue != NoneValue);

    HRESULT hr = S_OK;
    D3D_CHECK(m_pCommandList->Close());

    m_pendingFenceValue = m_currentFenceValue;
    m_currentFenceValue = NoneValue;

    return hr;
}

HRESULT CommandList::Submit(ID3D12CommandQueue* pQueue)
{
    assert(m_pendingFenceValue != NoneValue && m_submittedFenceValue == NoneValue && m_currentFenceValue == NoneValue);

    pQueue->ExecuteCommandLists(1, &m_pCommandListForSubmit);

    m_submittedFenceValue = m_pendingFenceValue;
    m_pendingFenceValue = NoneValue;

    return S_OK;
}

HRESULT CommandList::Wait(UINT64& finishedFenceValue)
{
    assert(m_pendingFenceValue == NoneValue && m_currentFenceValue == NoneValue);

    finishedFenceValue = NoneValue;

    HRESULT hr = S_OK;
    if (m_submittedFenceValue != NoneValue)
    {
        UINT64 completed = m_pFence->GetCompletedValue();
        if (completed == NoneValue || completed < m_submittedFenceValue)
        {
            D3D_CHECK(m_pFence->SetEventOnCompletion(m_submittedFenceValue, m_hEvent));
            if (SUCCEEDED(hr))
            {
                WaitForSingleObject(m_hEvent, INFINITE);
                completed = m_pFence->GetCompletedValue();
                assert(completed != NoneValue && completed >= m_submittedFenceValue);
            }
        }
        finishedFenceValue = m_submittedFenceValue;
        m_submittedFenceValue = NoneValue;
    }
    return hr;
}

bool CommandQueue::Init(ID3D12Device* pDevice, const D3D12_COMMAND_LIST_TYPE& type, int cmdListCount)
{
    bool res = true;
    for (int i = 0; i < cmdListCount && res; i++)
    {
        CommandList* pList = new CommandList();
        res = pList->Init(pDevice, type);
        if (res)
        {
            m_cmdLists.push_back(pList);
        }
        else
        {
            delete pList;
            pList = nullptr;
        }
    }

    if (res)
    {
        HRESULT hr = S_OK;

        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.Type = type;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        D3D_CHECK(pDevice->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), (void**)&m_pQueue));

        res = SUCCEEDED(hr);
    }

    if (res)
    {
        m_currentFenceValue = 1;
    }

    if (!res)
    {
        Term();
    }

    return res;
}

void CommandQueue::Term()
{
    UINT64 finishedFenceValue; // Will not be actually used here
    WaitIdle(finishedFenceValue);

    D3D_RELEASE(m_pQueue);

    for (auto pList : m_cmdLists)
    {
        pList->Term();
        delete pList;
    }
    m_cmdLists.clear();

    m_currentFenceValue = NoneValue;
    m_curCmdList = -1;
}

HRESULT CommandQueue::OpenCommandList(ID3D12GraphicsCommandList** ppList, UINT64& finishedFenceValue)
{
    m_curCmdList = (m_curCmdList + 1) % m_cmdLists.size();

    CommandList* pCmdList = m_cmdLists[m_curCmdList];

    finishedFenceValue = NoneValue;

    HRESULT hr = S_OK;
    D3D_CHECK(pCmdList->Wait(finishedFenceValue));
    D3D_CHECK(pCmdList->Open(m_currentFenceValue));

    if (SUCCEEDED(hr))
    {
        *ppList = pCmdList->GetGraphicsCommandList();
        ++m_currentFenceValue;
    }

    return hr;
}

HRESULT CommandQueue::CloseAndSubmitCommandList(UINT64* pSubmitFenceValue)
{
    if (pSubmitFenceValue != nullptr)
    {
        *pSubmitFenceValue = NoneValue;
    }

    CommandList* pCmdList = m_cmdLists[m_curCmdList];

    HRESULT hr = CloseCommandList();
    if (SUCCEEDED(hr))
    {
        hr = SubmitCommandList(pSubmitFenceValue);
    }

    return hr;
}

HRESULT CommandQueue::CloseCommandList()
{
    CommandList* pCmdList = m_cmdLists[m_curCmdList];

    HRESULT hr = S_OK;
    D3D_CHECK(pCmdList->Close());

    return hr;
}

HRESULT CommandQueue::SubmitCommandList(UINT64* pSubmitFenceValue)
{
    if (pSubmitFenceValue != nullptr)
    {
        *pSubmitFenceValue = NoneValue;
    }

    if (m_curCmdList == -1)
    {
        return S_OK;
    }

    CommandList* pCmdList = m_cmdLists[m_curCmdList];
    if (pCmdList->GetPendingFenceValue() == NoneValue)
    {
        return S_OK;
    }

    HRESULT hr = S_OK;
    D3D_CHECK(pCmdList->Submit(m_pQueue));
    D3D_CHECK(PreSignal());
    D3D_CHECK(m_pQueue->Signal(pCmdList->GetFence(), pCmdList->GetSubmittedFenceValue()));

    if (SUCCEEDED(hr))
    {
        if (pSubmitFenceValue != nullptr)
        {
            *pSubmitFenceValue = pCmdList->GetSubmittedFenceValue();
        }
    }

    return hr;
}

void CommandQueue::WaitIdle(UINT64& finishedFenceValue)
{
    finishedFenceValue = NoneValue;
    for (auto pCmdList : m_cmdLists)
    {
        UINT64 cmdListFinishedFenceValue = NoneValue;

        if (pCmdList->GetSubmittedFenceValue() != NoneValue)
        {
            pCmdList->Wait(cmdListFinishedFenceValue);
            if (cmdListFinishedFenceValue != NoneValue)
            {
                finishedFenceValue = cmdListFinishedFenceValue;
            }
        }
    }
}

HRESULT PresentCommandQueue::PreSignal()
{
    return m_pSwapchain->Present(m_vsync ? 1 : 0, 0);
}

} // Platform
