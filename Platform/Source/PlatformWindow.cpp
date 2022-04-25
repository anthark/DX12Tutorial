#include "stdafx.h"
#include "PlatformWindow.h"

#include <windowsx.h>

#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Platform::WindowCallback* pWCB = reinterpret_cast<Platform::WindowCallback*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    LRESULT imGuiRes = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    /*if (imGuiRes == 0)
    {
        return imGuiRes;
    }*/

    switch (msg)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            break;

        case WM_PAINT:
            if (pWCB != nullptr && !pWCB->GetRenderInProgress())
            {
                pWCB->SetRenderInProgress(true);
                bool res = pWCB->OnPaint();
                if (res)
                {
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    ValidateRect(hWnd, &rc);
                }
                pWCB->SetRenderInProgress(false);
                if (res)
                {
                    return 0;
                }
            }
            break;

        case WM_TIMER:
            if (pWCB != nullptr && pWCB->OnTimer((int)wParam))
            {
                return 0;
            }
            break;

        case WM_SIZE:
            if (pWCB != nullptr ? pWCB->OnSize() : false)
            {
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (pWCB != nullptr ? pWCB->OnKeyDown((int)wParam) : false)
            {
                return 0;
            }
            break;

        case WM_KEYUP:
            if (pWCB != nullptr ? pWCB->OnKeyUp((int)wParam) : false)
            {
                return 0;
            }
            break;

        case WM_KILLFOCUS:
            if (pWCB != nullptr ? pWCB->OnKillFocus() : false)
            {
                return 0;
            }
            break;

        case WM_RBUTTONDOWN:
            if (pWCB != nullptr && pWCB->OnRButtonPressed(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                SetCapture(hWnd);

                return 0;
            }
            break;

        case WM_LBUTTONDOWN:
            if (pWCB != nullptr && pWCB->OnLButtonPressed(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                SetCapture(hWnd);

                return 0;
            }
            break;

        case WM_RBUTTONUP:
            if ((wParam & (MK_LBUTTON | MK_RBUTTON)) == 0
                && GetCapture() == hWnd)
            {
                ReleaseCapture();
            }
            if (pWCB != nullptr && pWCB->OnRButtonReleased(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if ((wParam & (MK_LBUTTON | MK_RBUTTON)) == 0
                && GetCapture() == hWnd)
            {
                ReleaseCapture();
            }
            if (pWCB != nullptr && pWCB->OnLButtonReleased(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (pWCB != nullptr ? pWCB->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (int)wParam) : false)
            {
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            if (pWCB != nullptr && pWCB->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
            {
                return 0;
            }
            break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

}

namespace Platform
{

Window::~Window()
{
    assert(m_hWnd == nullptr);
}

bool Window::Create(const CreateAppWindowParams& params, HINSTANCE hInstance)
{
    return CreateAppWindow(params, hInstance, m_hWnd, this);
}

void Window::Destroy()
{
    m_hWnd = nullptr; // Actual window will be destroyed by OS
}

bool Window::OnSize()
{
    RECT rc;
    GetClientRect(m_hWnd, &rc);

    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Height = (FLOAT)(rc.bottom - rc.top);
    m_viewport.Width = (FLOAT)(rc.right - rc.left);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    m_rect.left = 0;
    m_rect.top = 0;
    m_rect.bottom = rc.bottom - rc.top;
    m_rect.right = rc.right - rc.left;

    return true;
}

PLATFORM_API bool CreateAppWindow(const CreateAppWindowParams& params, HINSTANCE hInstance, HWND& hWnd, WindowCallback* pWCB)
{
    hWnd = nullptr;

    WNDCLASS windowClass;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    if (params.filled)
    {
        LOGBRUSH brush;
        brush.lbColor = RGB(255, 255, 255);
        brush.lbHatch = 0;
        brush.lbStyle = BS_SOLID;

        windowClass.hbrBackground = CreateBrushIndirect(&brush);
    }
    else
    {
        windowClass.hbrBackground = nullptr;
    }
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = nullptr;
    windowClass.hInstance = hInstance;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = _T("PlatformWindowClass");
    windowClass.style = CS_VREDRAW | CS_HREDRAW;

    ATOM wndClass = RegisterClass(&windowClass);
    assert(wndClass != 0);

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!params.resizable)
    {
        style = style & ~WS_THICKFRAME;
    }

    RECT rc;
    rc.left = 100; 
    rc.top = 100;
    rc.right = rc.left + params.clientWidth;
    rc.bottom = rc.top + params.clientHeight;
    BOOL adjustRes = AdjustWindowRect(&rc, style, FALSE);
    assert(adjustRes == TRUE);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    hWnd = CreateWindow(
        windowClass.lpszClassName,
        params.windowName,
        style,
        0, 0, 
        width, height, 
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );
    assert(hWnd != nullptr);

    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pWCB);

    // Calculate initial pos
    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
    assert(monitor != nullptr);

    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    BOOL res = GetMonitorInfo(monitor, &mi);
    assert(res == TRUE);

    MoveWindow(hWnd, mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - width) / 2, mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - height) / 2, width, height, TRUE);

    ShowWindow(hWnd, SW_SHOW);

    return hWnd != nullptr;
}

PLATFORM_API int MainLoop(HWND idleWnd)
{
    MSG msg;
    while (true)
    {
        bool performIdle = true;
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return (int)msg.wParam;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_CHAR)
            {
                performIdle = false;
            }
        }
        if (performIdle)
        {
            // Idle callback
            Platform::WindowCallback* pWCB = reinterpret_cast<Platform::WindowCallback*>(GetWindowLongPtr(idleWnd, GWLP_USERDATA));
            if (pWCB != nullptr)
            {
                pWCB->OnIdle();
            }

            //Sleep(1);
        }
    }

    return 0;
}

} // Platform
