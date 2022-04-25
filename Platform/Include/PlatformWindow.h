#pragma once

namespace Platform
{

class PLATFORM_API WindowCallback
{
public:
    WindowCallback() : m_paintInProgress(false) {}
    virtual ~WindowCallback() {}

    virtual bool OnPaint() { return false; }
    virtual bool OnSize() { return false; }
    virtual bool OnTimer(int timerId) { return false; }

    virtual bool OnKeyDown(int virtualKeyCode) { return false; }
    virtual bool OnKeyUp(int virtualKeyCode) { return false; }
    virtual bool OnKillFocus() { return false; }

    virtual bool OnLButtonPressed(int x, int y) { return false; }
    virtual bool OnLButtonReleased(int x, int y) { return false; }
    virtual bool OnRButtonPressed(int x, int y) { return false; }
    virtual bool OnRButtonReleased(int x, int y) { return false; }
    virtual bool OnMouseMove(int x, int y, int flags) { return false; }
    virtual bool OnMouseWheel(int zDelta) { return false; }

    virtual void OnIdle() {}

    inline void SetRenderInProgress(bool paintInProgress) { m_paintInProgress = paintInProgress; }
    inline bool GetRenderInProgress() const { return m_paintInProgress; }

private:
    bool m_paintInProgress;
};

struct PLATFORM_API CreateAppWindowParams
{
    LPCTSTR windowName;
    int clientWidth;
    int clientHeight;
    bool resizable;
    bool filled;
};

class PLATFORM_API Window : public WindowCallback
{
public:
    Window() : WindowCallback(), m_hWnd(nullptr) {}
    virtual ~Window();

    virtual bool Create(const CreateAppWindowParams& params, HINSTANCE hInstance);
    virtual void Destroy();

    inline HWND GetHWND() const { return m_hWnd; }

    virtual bool OnSize() override;

protected:
    inline bool IsMinimized() const {
        return m_rect.bottom - m_rect.top == 0 || m_rect.right - m_rect.left == 0;
    }

protected:
    HWND m_hWnd;
    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_rect;
};

PLATFORM_API bool CreateAppWindow(const CreateAppWindowParams& params, HINSTANCE hInstance, HWND& hWnd, WindowCallback* pWCB);
PLATFORM_API int  MainLoop(HWND idleWnd = nullptr);

} // Platform
