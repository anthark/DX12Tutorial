#pragma once

#include "PlatformWindow.h"
#include "CameraControl/PlatformCameraControl.h"

namespace Platform
{

class Device;

// Renderer interface
class PLATFORM_API Renderer
{
public:
    Renderer(Device* pDevice) : m_pDevice(pDevice), m_usec(0), m_initialUsec(0) {}

    virtual bool Init(HWND hWnd);
    virtual void Term() {}

    virtual bool Update(double elapsedSec, double deltaSec) = 0;
    virtual bool Render() = 0;
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect);

    bool Update();

    inline Device* GetDevice() const { return m_pDevice; }

protected:
    inline D3D12_RECT GetRect() const { return m_rect; }
    inline D3D12_VIEWPORT GetViewport() const { return m_vewport; }

    inline HWND GetHwnd() const { return m_hWnd; }

private:
    HWND m_hWnd;

    Device* m_pDevice;

    D3D12_VIEWPORT m_vewport;
    D3D12_RECT m_rect;

    size_t m_usec; // Current update moment in microseconds since first update
    size_t m_initialUsec; // Initial update moment
};

// Window with integrated renderer and common camera control code
class PLATFORM_API RenderWindow : public Window
{
public:
    RenderWindow(Renderer* pRenderer, CameraControl* pCameraControl);

    virtual bool OnSize() override;

    virtual void OnIdle() override;

    virtual bool OnKeyDown(int virtualKeyCode) override;
    virtual bool OnKeyUp(int virtualKeyCode) override;
    virtual bool OnKillFocus() override;

    virtual bool OnRButtonPressed(int x, int y) override;
    virtual bool OnRButtonReleased(int x, int y) override;
    virtual bool OnMouseMove(int x, int y, int flags) override;
    virtual bool OnMouseWheel(int zDelta) override;

private:
    void ResetButtons();

private:
    Renderer* m_pRenderer;
    CameraControl* m_pCameraControl;

    bool m_pressedButtons[0x100];
};

} // Platform
