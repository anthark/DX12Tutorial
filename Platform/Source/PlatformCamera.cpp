#include "stdafx.h"
#include "PlatformCamera.h"

namespace Platform
{

Camera::Camera()
    : m_projection(Perspective)
    , m_near(0.1f)
    , m_far(1000.0f)
    , m_horzFov((float)M_PI / 3)
    , m_lat((float)M_PI / 4)
    , m_lon((float)M_PI / 4)
    , m_roll(0.0)
    , m_lookAt{ 0, 0, 0 }
    , m_distance(5)
{}

void Camera::SetHorzFOV(float fov)
{
    assert(m_projection == Perspective);
    m_horzFov = fov;
}

float Camera::GetHorzFOV() const
{
    assert(m_projection == Perspective);
    return m_horzFov;
}

void Camera::SetHorzScale(float scale)
{
    assert(m_projection == Orthographic);

    m_orthoPos = Point2f{ -scale * 0.5f, -scale * 0.5f };
    m_orthoScale = Point2f{ scale, scale };
}

void Camera::SetRect(float left, float bottom, float right, float top)
{
    assert(m_projection == Orthographic);

    m_orthoPos = Point2f{left, bottom};
    m_orthoScale = Point2f{ right, top } - m_orthoPos;
}

Point4f Camera::CalcPos() const
{
    Point3f right, up, dir;
    CalcDirection(right, up, dir);

    Point3f pos = m_lookAt - dir * m_distance;

    return Point4f{ pos.x, pos.y, pos.z, 1.0f };
}

Matrix4f Camera::CalcViewMatrix() const
{
    return CalcInverseViewMatrix().Inverse();
}

Matrix4f Camera::CalcViewMatrixNoTrans() const
{
    Point3f right, up, dir;
    CalcDirection(right, up, dir);

    Matrix4f view;
    view.CoordTransformMatrix(right, up, dir, Point3f(0,0,0));

    return view.Inverse();
}

Matrix4f Camera::CalcInverseViewMatrix() const
{
    Point3f right, up, dir;
    CalcDirection(right, up, dir);

    Point3f pos = m_lookAt - dir * m_distance;

    Matrix4f view;
    view.CoordTransformMatrix(right, up, dir, pos);

    return view;
}

Matrix4f Camera::CalcProjMatrix(float aspectRatioHdivW) const
{
    if (m_projection == Perspective)
    {
        Matrix4f proj;
        proj.Zero();

        float nearWidth = tanf(m_horzFov * 0.5f) * m_near * 2.0f;
        float nearHeight = aspectRatioHdivW * nearWidth;

        proj.m[0] = 2.0f * m_near / nearWidth;
        proj.m[5] = 2.0f * m_near / nearHeight;
        proj.m[10] = m_far / (m_far - m_near);
        proj.m[11] = 1.0f;
        proj.m[14] = -m_near * m_far / (m_far - m_near);

        return proj;
    }
    else if (m_projection == Orthographic)
    {
        float right = m_orthoScale.x + m_orthoPos.x;
        float left = m_orthoPos.x;
        float top = m_orthoScale.y + m_orthoPos.y;
        float bottom = m_orthoPos.y;

        Matrix4f proj;
        proj.Identity();

        proj.m[0] = 2.0f / (right - left);
        proj.m[12] = -(right + left) / (right - left);

        proj.m[5] = 2.0f / (top - bottom);
        proj.m[13] = -(top + bottom) / (top - bottom);

        proj.m[10] = 1.0f / (m_far - m_near);
        proj.m[14] = -m_near / (m_far - m_near);

        proj.m[15] = 1.0f;

        return proj;
    }

    assert(0);
    return Matrix4f();
}

void Camera::CalcDirection(Point3f& right, Point3f& up, Point3f& dir) const
{
    dir = -Point3f{ cosf(m_lon) * cosf(m_lat), sinf(m_lat), sinf(m_lon) * cosf(m_lat) };
    dir.normalize();

    right = Point3f{ cosf(m_lon + (float)M_PI / 2.0f), 0, sinf(m_lon + (float)M_PI / 2.0f) };
    right.normalize();

    up = dir.cross(right);

    // Apply roll
    Matrix4f rollMatrix;
    rollMatrix.Rotation(m_roll, -dir);

    right = rollMatrix * Point4f(right,1);
    up = rollMatrix * Point4f(up, 1);
}

void Camera::CalcFrustumPoints(std::vector<Point3f>& pts, float nearPlane, float farPlane, float aspectRatioHdivW) const
{
    assert(m_projection == Perspective);

    Point3f right, up, dir;
    CalcDirection(right, up, dir);
    Point4f pos = CalcPos();

    Point3f center = Point3f{ pos.x, pos.y, pos.z } + dir * nearPlane;
    float halfWidth = tanf(m_horzFov / 2.0f) * nearPlane;
    float halfHeight = halfWidth * aspectRatioHdivW;

    pts.push_back(center - right * halfWidth - up * halfHeight);
    pts.push_back(center + right * halfWidth - up * halfHeight);
    pts.push_back(center + right * halfWidth + up * halfHeight);
    pts.push_back(center - right * halfWidth + up * halfHeight);

    center = Point3f{ pos.x, pos.y, pos.z } + dir * farPlane;
    halfWidth = tanf(m_horzFov / 2.0f) * farPlane;
    halfHeight = halfWidth * aspectRatioHdivW;

    pts.push_back(center - right * halfWidth - up * halfHeight);
    pts.push_back(center + right * halfWidth - up * halfHeight);
    pts.push_back(center + right * halfWidth + up * halfHeight);
    pts.push_back(center - right * halfWidth + up * halfHeight);
}

} // Platform
