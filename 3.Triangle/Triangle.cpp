#include "stdafx.h"

#include "PlatformWindow.h"
#include "PlatformDevice.h"

#include "Renderer.h"

class MainWindow : public Platform::Window
{
public:
    MainWindow(Renderer* pRenderer) : Platform::Window(), m_pRenderer(pRenderer) {}

protected:
    virtual bool OnPaint() override
    {
        if (IsMinimized())
        {
            return false;
        }

        return m_pRenderer->Render(m_viewport, m_rect);
    }

    virtual bool OnSize() override
    {
        bool res = Platform::Window::OnSize();

        if (res && m_pRenderer->GetDevice() != nullptr)
        {
            return res && m_pRenderer->GetDevice()->ResizeSwapchain(m_rect.right - m_rect.left, m_rect.bottom - m_rect.top);
        }

        return res;
    }

private:
    Renderer* m_pRenderer;
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    int exitCode = -1;

    Platform::Device* pDevice = new Platform::Device();
    Renderer* pRenderer = new Renderer();
    MainWindow* pWindow = new MainWindow(pRenderer);
    if (pWindow->Create({ _T("Triangle"), 1280, 720, true, false }, hInstance))
    {
#ifdef _DEBUG
        Platform::DeviceCreateParams params{ true, true, 3, 2, pWindow->GetHWND() };
#else
        Platform::DeviceCreateParams params{ false, false, 3, 2, pWindow->GetHWND() };
#endif
        if (pDevice->Create(params))
        {
            if (pRenderer->Init(pDevice))
            {
                exitCode = Platform::MainLoop();
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
