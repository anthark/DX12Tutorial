#include "stdafx.h"

#include "PlatformWindow.h"
#include "PlatformDevice.h"

class MainWindow : public Platform::Window
{
public:
    MainWindow(Platform::Device* pDevice) : Platform::Window(), m_pDevice(pDevice) {}

protected:
    virtual bool OnPaint() override
    {
        if (IsMinimized())
        {
            return false;
        }

        ID3D12GraphicsCommandList* pCommandList = nullptr;
        ID3D12Resource* pBackBuffer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
        if (m_pDevice->BeginRenderCommandList(&pCommandList, &pBackBuffer, &rtvHandle))
        {
            HRESULT hr = S_OK;
            pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, nullptr);

            pCommandList->RSSetViewports(1, &m_viewport);
            pCommandList->RSSetScissorRects(1, &m_rect);

            if (m_pDevice->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET))
            {
                FLOAT clearColor[4] = { 0.0f, 0.5f, 0.0f, 1.0f };
                pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &m_rect);

                m_pDevice->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            }

            m_pDevice->CloseSubmitAndPresentRenderCommandList();
        }

        return true;
    }

private:
    Platform::Device* m_pDevice;
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    int exitCode = -1;

    Platform::Device* pDevice = new Platform::Device();
    MainWindow* pWindow = new MainWindow(pDevice);
    if (pWindow->Create({ _T("CreateDevice"), 1280, 720, true, false }, hInstance))
    {
#ifdef _DEBUG
        Platform::DeviceCreateParams params{ true, true, 3, 2, pWindow->GetHWND() };
#else
        Platform::DeviceCreateParams params{ false, false, 3, 2, pWindow->GetHWND() };
#endif
        if (pDevice->Create(params))
        {
            exitCode = Platform::MainLoop();

            pDevice->Destroy();
        }

        pWindow->Destroy();
    }
    delete pWindow;
    pWindow = nullptr;

    delete pDevice;
    pDevice = nullptr;

    return exitCode;
}
