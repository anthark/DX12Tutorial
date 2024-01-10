#include "stdafx.h"

#include "PlatformWindow.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    int exitCode = -1;
    Platform::Window* pWindow = new Platform::Window();
    if (pWindow->Create({ _T("Window"), 1280, 720, true, true }, hInstance))
    {
        exitCode = Platform::MainLoop();
        pWindow->Destroy();
    }
    delete pWindow;
    pWindow = nullptr;

    return exitCode;
}
