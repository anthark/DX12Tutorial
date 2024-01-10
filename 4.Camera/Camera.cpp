#include "stdafx.h"

#include "PlatformWindow.h"
#include "PlatformDevice.h"

#include "Renderer.h"

class MainWindow : public Platform::Window
{
public:
    MainWindow(Renderer* pRenderer)
        : Platform::Window()
        , m_pRenderer(pRenderer)
        , m_forwardAccel(0)
        , m_rightAccel(0)
    {
        ResetButtons();
    }

    virtual bool Create(const Platform::CreateAppWindowParams& params, HINSTANCE hInstance) override
    {
        bool res = Platform::Window::Create(params, hInstance);
        if (res)
        {
            SetTimer(m_hWnd, 1, USER_TIMER_MINIMUM, nullptr);
        }
        return res;
    }

    virtual void Destroy() override
    {
        KillTimer(m_hWnd, 1);

        Platform::Window::Destroy();
    }

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

    virtual bool OnTimer(int timerId) override
    {
        if (m_hWnd != nullptr)
        {
            RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE);
        }

        return true;
    }

    virtual bool OnKeyDown(int virtualKeyCode) override
    {
        if (!m_pressedButtons[virtualKeyCode])
        {
            bool update = false;
            switch (virtualKeyCode)
            {
                case 'W':
                    m_forwardAccel += 1;
                    update = true;
                    break;

                case 'S':
                    m_forwardAccel -= 1;
                    update = true;
                    break;

                case 'D':
                    m_rightAccel += 1;
                    update = true;
                    break;

                case 'A':
                    m_rightAccel -= 1;
                    update = true;
                    break;
            }

            m_pressedButtons[virtualKeyCode] = true;

            if (update)
            {
                m_pRenderer->SetAccelerator(m_forwardAccel, m_rightAccel);
                return true;
            }
        }

        return false;
    }

    virtual bool OnKeyUp(int virtualKeyCode) override
    {
        if (m_pressedButtons[virtualKeyCode])
        {
            bool update = false;
            switch (virtualKeyCode)
            {
                case 'W':
                    m_forwardAccel -= 1;
                    update = true;
                    break;

                case 'S':
                    m_forwardAccel += 1;
                    update = true;
                    break;

                case 'D':
                    m_rightAccel -= 1;
                    update = true;
                    break;

                case 'A':
                    m_rightAccel += 1;
                    update = true;
                    break;
            }

            m_pressedButtons[virtualKeyCode] = false;

            if (update)
            {
                m_pRenderer->SetAccelerator(m_forwardAccel, m_rightAccel);
                return true;
            }
        }

        return false;
    }

    virtual bool OnKillFocus() override
    {
        m_forwardAccel = 0;
        m_rightAccel = 0;
        ResetButtons();
        m_pRenderer->SetAccelerator(m_forwardAccel, m_rightAccel);

        return true;
    }

    virtual bool OnRButtonPressed(int x, int y) override
    {
        return m_pRenderer->OnRButtonPressed(x, y);
    }
    virtual bool OnRButtonReleased(int x, int y) override
    {
        return m_pRenderer->OnRButtonReleased(x, y);
    }
    virtual bool OnMouseMove(int x, int y, int flags) override
    {
        return m_pRenderer->OnMouseMove(x, y, flags, m_rect);
    }
    virtual bool OnMouseWheel(int zDelta) override
    {
        return m_pRenderer->OnMouseWheel(zDelta);
    }

private:
    void ResetButtons()
    {
        memset(m_pressedButtons, 0, sizeof(m_pressedButtons));
    }


private:
    float m_forwardAccel;
    float m_rightAccel;

    bool m_pressedButtons[0x100];

    Renderer* m_pRenderer;
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    int exitCode = -1;

    Platform::Device* pDevice = new Platform::Device();
    Renderer* pRenderer = new Renderer();
    MainWindow* pWindow = new MainWindow(pRenderer);
    if (pWindow->Create({ _T("Camera"), 1280, 720, true, false }, hInstance))
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
