#pragma once

#include "PlatformPoint.h"
#include "PlatformMatrix.h"

namespace Platform
{

class PLATFORM_API Camera
{
public:

    enum Projection
    {
        Orthographic = 0,
        Perspective
    };

public:
    Camera();

    inline void SetProjection(const Projection& projection) { m_projection = projection; }
    inline Projection GetProjection() const { return m_projection; }

    inline void SetNear(float nearPlane) { m_near = nearPlane; }
    inline float GetNear() const { return m_near; }

    inline void SetFar(float farPlane) { m_far = farPlane; }
    inline float GetFar() const { return m_far; }

    void SetHorzFOV(float fov);
    float GetHorzFOV() const;

    void SetHorzScale(float scale);

    void SetRect(float left, float bottom, float right, float top);

    inline void SetLat(float lat) { m_lat = lat; }
    inline float GetLat() const { return m_lat; }

    inline void SetLon(float lon) { m_lon = lon; }
    inline float GetLon() const { return m_lon; }

    inline void SetRoll(float roll) { m_roll = roll; }
    inline float GetRoll() const { return m_roll; }

    inline void SetDistance(float distance) { m_distance = distance; }
    inline float GetDistance() const { return m_distance; }

    inline void SetLookAt(const Point3f& lookAt) { m_lookAt = lookAt; }
    inline Point3f GetLookAt() const { return m_lookAt; }

    Point4f CalcPos() const;
    Matrix4f CalcViewMatrix() const;
    Matrix4f CalcInverseViewMatrix() const;
    Matrix4f CalcProjMatrix(float aspectRatioHdivW) const;
    void CalcDirection(Point3f& right, Point3f& up, Point3f& dir) const;
    void CalcFrustumPoints(std::vector<Point3f>& pts, float nearPlane, float farPlane, float aspectRatioHdivW) const;

private:
    Projection m_projection;
    float m_near;
    float m_far;
    float m_horzFov;

    Point2f m_orthoPos;
    Point2f m_orthoScale;

    float m_lat;
    float m_lon;
    float m_roll;

    Point3f m_lookAt;
    float m_distance;
};

} // Platform
