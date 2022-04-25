#include "stdafx.h"
#include "PlatformRenderWindow.h"

#include "PlatformDevice.h"

#include <chrono>

namespace Platform
{

bool Renderer::Init(HWND hWnd)
{
    m_hWnd = hWnd;

    return m_pDevice->IsInitialized() && m_pDevice->ResizeSwapchain(m_rect.right - m_rect.left, m_rect.bottom - m_rect.top);
}

bool Renderer::Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect)
{
    m_rect = rect;
    m_vewport = viewport;

    return m_pDevice->IsInitialized() && m_pDevice->ResizeSwapchain(rect.right - rect.left, rect.bottom - rect.top);
}

bool Renderer::Update()
{
    size_t usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_initialUsec == 0)
    {
        m_initialUsec = usec; // Initial update
    }
    usec = usec - m_initialUsec;

    double deltaSec = (usec - m_usec) / 1000000.0;
    double elapsedSec = usec / 1000000.0;

    m_usec = usec;

    return Update(elapsedSec, deltaSec);
}

RenderWindow::RenderWindow(Renderer* pRenderer, CameraControl* pCameraControl)
    : Window()
    , m_pRenderer(pRenderer)
    , m_pCameraControl(pCameraControl)
{
    ResetButtons();
}

bool RenderWindow::OnSize()
{
    bool res = Platform::Window::OnSize();

    if (res && !IsMinimized())
    {
        return res && m_pRenderer->Resize(m_viewport, m_rect);
    }

    return res;
}

void RenderWindow::OnIdle()
{
    if (IsMinimized())
    {
        return;
    }

    if (m_pRenderer->Update())
    {
        m_pRenderer->Render();
    }
}

bool RenderWindow::OnKeyDown(int virtualKeyCode)
{
    if (!m_pressedButtons[virtualKeyCode])
    {
        bool processed = m_pCameraControl->OnKeyDown(virtualKeyCode);
        m_pressedButtons[virtualKeyCode] = true;

        return processed;
    }

    return false;
}

bool RenderWindow::OnKeyUp(int virtualKeyCode)
{
    if (m_pressedButtons[virtualKeyCode])
    {
        bool processed = m_pCameraControl->OnKeyUp(virtualKeyCode);
        m_pressedButtons[virtualKeyCode] = false;

        return processed;
    }

    return false;
}

bool RenderWindow::OnKillFocus()
{
    ResetButtons();
    m_pCameraControl->OnKillFocus();

    return true;
}

bool RenderWindow::OnRButtonPressed(int x, int y)
{
    return m_pCameraControl->OnRButtonPressed(x, y);
}

bool RenderWindow::OnRButtonReleased(int x, int y)
{
    return m_pCameraControl->OnRButtonReleased(x, y);
}

bool RenderWindow::OnMouseMove(int x, int y, int flags)
{
    return m_pCameraControl->OnMouseMove(x, y, flags, m_rect);
}

bool RenderWindow::OnMouseWheel(int zDelta)
{
    return m_pCameraControl->OnMouseWheel(zDelta);
}

void RenderWindow::ResetButtons()
{
    memset(m_pressedButtons, 0, sizeof(m_pressedButtons));
}

} // Platform
