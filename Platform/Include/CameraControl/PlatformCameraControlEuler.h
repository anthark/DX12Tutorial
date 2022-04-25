#pragma once

#include "PlatformCameraControl.h"

namespace Platform
{

class PLATFORM_API CameraControlEuler : public CameraControl
{
public:
    CameraControlEuler();
    virtual ~CameraControlEuler() {}

    virtual bool OnKeyDown(int virtualKeyCode) override;
    virtual bool OnKeyUp(int virtualKeyCode) override;
    virtual bool OnKillFocus() override;

    virtual bool OnRButtonPressed(int x, int y) override;
    virtual bool OnRButtonReleased(int x, int y) override;
    virtual bool OnMouseMove(int x, int y, int flags, const RECT& rect) override;
    virtual bool OnMouseWheel(int zDelta) override;

    virtual Point3f UpdateCamera(double deltaSec) override;

protected:
    float m_cameraMoveSpeed;
    float m_cameraRotateSpeed;

    bool m_rbPressed;
    int m_prevRbX;
    int m_prevRbY;

    // Camera movement parameters
    float m_forwardAccel;
    float m_rightAccel;
    float m_angleDeltaX;
    float m_angleDeltaY;
};

} // Platform
