#include "stdafx.h"

#include "PlatformRenderWindow.h"
#include "PlatformDevice.h"

#include "Renderer.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    int exitCode = -1;

    Platform::Device* pDevice = new Platform::Device();
    Renderer* pRenderer = new Renderer(pDevice);
    Platform::RenderWindow* pWindow = new Platform::RenderWindow(pRenderer, pRenderer);
    if (pWindow->Create({ _T("Particles"), 1920, 1080, true, false }, hInstance))
    {
        // We use more memory for dynamic buffers here, as vsync is turned off for this sample to compare GPU performance
        // Hence it can be a lot of frames generated on CPU and once GPU is delayed a little bit, when switching to/from full screen mode, it can lead to lack of dynamic memory
        // Looks like 64 is enough for Nvidia RTX 2070 and 4k monitor, but I reserve 128 just in case
#ifdef _DEBUG
        Platform::DeviceCreateParams params{ true, true, 3, 2, pWindow->GetHWND(), 512, 128 };
#else
        Platform::DeviceCreateParams params{ false, false, 3, 2, pWindow->GetHWND(), 512, 128 };
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
