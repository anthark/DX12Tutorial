#include "stdafx.h"

#include "PlatformRenderWindow.h"
#include "PlatformDevice.h"

#include "Renderer.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    int exitCode = -1;

    Platform::Device* pDevice = new Platform::Device();
    Renderer* pRenderer = new Renderer(pDevice);
    Platform::RenderWindow* pWindow = new Platform::RenderWindow(pRenderer, pRenderer);
    if (pWindow->Create({ _T("HDR"), 1920, 1080, true, false }, hInstance))
    {
#ifdef _DEBUG
        Platform::DeviceCreateParams params{ true, true, 3, 2, pWindow->GetHWND() };
#else
        Platform::DeviceCreateParams params{ false, false, 3, 2, pWindow->GetHWND() };
#endif
        if (pDevice->Create(params))
        {
            if (pRenderer->Init(pWindow->GetHWND()))
            {
                exitCode = Platform::MainLoop(pWindow->GetHWND());
                pRenderer->Term();
            }

            pDevice->Destroy();
        }

        pWindow->Destroy();
    }
    delete pWindow;
    pWindow = nullptr;

    delete pRenderer;
    pRenderer = nullptr;

    delete pDevice;
    pDevice = nullptr;

    return exitCode;
}
