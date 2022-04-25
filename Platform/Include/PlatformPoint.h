#pragma once

template <typename T>
struct Point2
{
    T x, y;

    inline Point2<T> operator-(const Point2<T>& rhs) const
    {
        return Point2<T>{x - rhs.x, y - rhs.y};
    }
};

template <typename T>
struct Point3
{
    T x, y, z;

    Point3<T>(const T& _x = (T)0, const T& _y = (T)0, const T& _z = (T)0)
        : x(_x)
        , y(_y)
        , z(_z)
    {}

    inline T lengthSqr() const { return x * x + y * y + z * z; }
    inline T length() const { return (T)sqrt(lengthSqr()); }

    void normalize()
    {
        T len = length();
        x /= len;
        y /= len;
        z /= len;
    }

    inline Point3<T> operator-() const
    {
        return Point3<T>{-x, -y, -z};
    }

    inline Point3<T> operator-(const Point3<T>& rhs) const
    {
        return Point3<T>{x - rhs.x, y - rhs.y, z - rhs.z};
    }

    inline Point3<T> operator+(const Point3<T>& rhs) const
    {
        return Point3<T>{x + rhs.x, y + rhs.y, z + rhs.z};
    }

    inline Point3<T> cross(const Point3<T>& rhs) const
    {
        return Point3<T>{y * rhs.z - z * rhs.y, -x * rhs.z + z * rhs.x, x * rhs.y - y * rhs.x};
    }

    inline T dot(const Point3<T>& rhs) const
    {
        return T(x * rhs.x + y * rhs.y + z * rhs.z);
    }

    inline Point3<T> operator*(const T& a) const
    {
        return Point3<T>{x * a, y * a, z * a};
    }
};

template <typename T>
struct Point4
{
    T x, y, z, w;

    Point4<T>(const T& _x = (T)0, const T& _y = (T)0, const T& _z = (T)0, const T& _w = (T)0)
        : x(_x)
        , y(_y)
        , z(_z)
        , w(_w)
    {}

    Point4<T>(const Point3<T>& p, const T& _w = (T)0)
        : x(p.x)
        , y(p.y)
        , z(p.z)
        , w(_w)
    {}

    inline T lengthSqr() const { return x * x + y * y + z * z + w*w; }
    inline T length() const { return (T)sqrt(lengthSqr()); }

    void normalize()
    {
        T len = length();
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }

    inline Point4<T> operator*(const T& a) const
    {
        return Point4<T>{x * a, y * a, z * a, w * a};
    }

    inline Point4<T> operator+(const Point4<T>& a) const
    {
        return Point4<T>{x + a.x, y + a.y, z + a.z, w + a.w};
    }

    inline Point4<T> operator-() const
    {
        return Point4<T>{-x, -y, -z, -w};
    }

    operator Point3<T>() const
    {
        return Point3<T>{x, y, z};
    }

    T dot(const Point4<T>& rhs) const
    {
        return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
    }

    /** Spherical interpolation of quaternions */
    inline static Point4<T> Slerp(const Point4<T>& a, const Point4<T>& b, T t)
    {
        T cosOmega = a.dot(b);
        // Work with negative b
        Point4<T> b0 = cosOmega < (T)0 ? -b : b;
        if (cosOmega < 0)
        {
            cosOmega = -cosOmega;
        }

        T scaleA, scaleB;
        // Normal case
        if (fabs((T)1 - cosOmega) > 0.0001f)
        {
            T omega = acosf(cosOmega);
            T invSin = (T)1 / sinf(omega);
            scaleA = sinf(((T)1 - t) * omega) * invSin;
            scaleB = sinf(t * omega) * invSin;
        }
        else
        {
            // Border case - linear
            scaleA = (T)1 - t;
            scaleB = t;
        }

        return a * scaleA + b0 * scaleB;
    }
};

using Point2f = Point2<float>;
using Point3f = Point3<float>;
using Point4f = Point4<float>;

using Point2i = Point2<int>;
using Point3i = Point3<int>;
using Point4i = Point4<int>;

using float2 = Point2f;
using float3 = Point3f;
using float4 = Point4f;

using int2 = Point2i;
using int3 = Point3i;
using int4 = Point4i;
