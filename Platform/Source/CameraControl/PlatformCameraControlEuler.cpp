#include "../stdafx.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include <algorithm>

namespace Platform
{

CameraControlEuler::CameraControlEuler()
    : m_cameraMoveSpeed(1.0f) // Linear units in 1 second
    , m_cameraRotateSpeed((float)M_PI) // Radians in from-border-to-border mouse move
    , m_rbPressed(false)
    , m_prevRbX(0)
    , m_prevRbY(0)
    , m_forwardAccel(0.0f)
    , m_rightAccel(0.0f)
    , m_angleDeltaX(0.0f)
    , m_angleDeltaY(0.0f)
{}

bool CameraControlEuler::OnKeyDown(int virtualKeyCode)
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

    return update;
}

bool CameraControlEuler::OnKeyUp(int virtualKeyCode)
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

    return update;
}

bool CameraControlEuler::OnKillFocus()
{
    m_forwardAccel = 0;
    m_rightAccel = 0;

    return true;
}

bool CameraControlEuler::OnRButtonPressed(int x, int y)
{
    m_rbPressed = true;
    m_prevRbX = x;
    m_prevRbY = y;

    return true;
}

bool CameraControlEuler::OnRButtonReleased(int x, int y)
{
    m_rbPressed = false;
    m_prevRbX = x;
    m_prevRbY = y;

    return true;
}

bool CameraControlEuler::OnMouseMove(int x, int y, int flags, const RECT& rect)
{
    if (m_rbPressed)
    {
        m_angleDeltaX += (float)(x - m_prevRbX) / (rect.right - rect.left);
        m_angleDeltaY += (float)(m_prevRbY - y) / (rect.bottom - rect.top);

        m_prevRbX = x;
        m_prevRbY = y;

        return true;
    }

    return false;
}

bool CameraControlEuler::OnMouseWheel(int zDelta)
{
    float distance = std::max(0.0f, m_camera.GetDistance() - zDelta / 120.0f);
    m_camera.SetDistance(distance);

    return true;
}

Point3f CameraControlEuler::UpdateCamera(double deltaSec)
{
    float lat = m_camera.GetLat() - m_angleDeltaY * m_cameraRotateSpeed;
    lat = std::min(std::max(lat, -(float)M_PI / 2), (float)M_PI / 2);
    m_camera.SetLat(lat);

    float lon = m_camera.GetLon() - m_angleDeltaX * m_cameraRotateSpeed;
    m_camera.SetLon(lon);

    m_angleDeltaX = m_angleDeltaY = 0.0f;

    Point3f right, up, dir;
    m_camera.CalcDirection(right, up, dir);

    Point3f forward;
    if (m_camera.GetLat() > (float)M_PI / 4 || m_camera.GetLat() < -(float)M_PI / 4)
    {
        forward = up;
    }
    else
    {
        forward = dir;
    }
    forward.y = 0.0f;
    forward.normalize();

    float lookAtDistance = std::max(1.0f, m_camera.GetDistance());

    Point3f cameraMoveDir = forward * m_forwardAccel + right * m_rightAccel;
    if (cameraMoveDir.lengthSqr() > 0.00001f)
    {
        cameraMoveDir.normalize();
        m_camera.SetLookAt(m_camera.GetLookAt() + cameraMoveDir * m_cameraMoveSpeed * lookAtDistance * (float)deltaSec);
    }

    return cameraMoveDir;
}

} // Platform
