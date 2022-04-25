#pragma once

#include "PlatformCamera.h"

namespace Platform
{

class PLATFORM_API CameraControl
{
public:

    virtual bool OnKeyDown(int virtualKeyCode) = 0;
    virtual bool OnKeyUp(int virtualKeyCode) = 0;
    virtual bool OnKillFocus() = 0;

    virtual bool OnRButtonPressed(int x, int y) = 0;
    virtual bool OnRButtonReleased(int x, int y) = 0;
    virtual bool OnMouseMove(int x, int y, int flags, const RECT& rect) = 0;
    virtual bool OnMouseWheel(int zDelta) = 0;

    virtual Point3f UpdateCamera(double deltaSec) = 0;

protected:
    inline const Platform::Camera* GetCamera() const { return &m_camera; }

protected:
    Platform::Camera m_camera;
};

} // Platform
