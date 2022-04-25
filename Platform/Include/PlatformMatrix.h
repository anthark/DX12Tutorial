#pragma once

#include "PlatformPoint.h"

template <typename T>
struct Matrix4
{
    Matrix4()
    {
        Identity();
    }

    void Zero()
    {
        memset(m, 0, sizeof(m));
    }

    void Identity()
    {
        Zero();
        m[0] = m[5] = m[10] = m[15] = T(1);
    }

    void Rotation(const T& alpha, const Point3<T>& axis)
    {
        Identity();

        T c = cos(alpha);
        T s = sin(alpha);

        m[0] = c + (T(1) - c) * axis.x * axis.x;
        m[1] = (T(1) - c)*axis.x*axis.y - s * axis.z;
        m[2] = (T(1) - c)*axis.x*axis.z + s * axis.y;

        m[4] = (T(1) - c)*axis.y*axis.x + s * axis.z;
        m[5] = c + (T(1) - c)*axis.y*axis.y;
        m[6] = (T(1) - c)*axis.y*axis.z - s * axis.x;

        m[8] = (T(1) - c)*axis.z*axis.x - s * axis.y;
        m[9] = (T(1) - c)*axis.z*axis.y + s * axis.x;
        m[10] = c + (T(1) - c)*axis.z*axis.z;
    }

    Matrix4<T>& Offset(const Point3<T>& offset)
    {
        Identity();

        m[12] = offset.x;
        m[13] = offset.y;
        m[14] = offset.z;

        return *this;
    }

    Matrix4<T>& Scale(T sx, T sy, T sz)
    {
        Identity();

        m[0] = sx;
        m[5] = sy;
        m[10] = sz;

        return *this;
    }

    Matrix4<T> operator*(const Matrix4<T>& b) const
    {
        Matrix4 newM;

        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                T sum = T(0);
                for (int k = 0; k < 4; k++)
                {
                    sum += m[i * 4 + k] * b.m[k * 4 + j];
                }
                newM.m[i * 4 + j] = sum;
            }
        }

        return newM;
    }

    Point4<T> operator*(const Point4<T>& p) const
    {
        Point4<T> res = { 0 };

        res.x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * p.w;
        res.y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * p.w;
        res.z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * p.w;
        res.w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * p.w;

        /*for (int i = 0; i < 4; i++)
        {
            res.x += m[i * 4 + 0] * p.x;
            res.y += m[i * 4 + 1] * p.y;
            res.z += m[i * 4 + 2] * p.z;
            res.w += m[i * 4 + 3] * p.w;
        }*/

        return res;
    }

    void CoordTransformMatrix(const Point3<T>& xaxis, const Point3<T>& yaxis, const Point3<T>& zaxis, const Point3<T>& origin)
    {
        Identity();

        m[0] = xaxis.x;
        m[1] = xaxis.y;
        m[2] = xaxis.z;

        m[4] = yaxis.x;
        m[5] = yaxis.y;
        m[6] = yaxis.z;

        m[8] = zaxis.x;
        m[9] = zaxis.y;
        m[10] = zaxis.z;

        m[12] = origin.x;
        m[13] = origin.y;
        m[14] = origin.z;
    }

    Matrix4<T> Transpose()
    {
        Matrix4<T> trans;
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                trans.m[i * 4 + j] = m[j * 4 + i];
            }
        }

        return trans;
    }

    Matrix4<T> Inverse() const
    {
        Matrix4<T> inv;

        inv.m[0] = m[5] * m[10] * m[15] -
            m[5] * m[11] * m[14] -
            m[9] * m[6] * m[15] +
            m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] -
            m[13] * m[7] * m[10];

        inv.m[4] = -m[4] * m[10] * m[15] +
            m[4] * m[11] * m[14] +
            m[8] * m[6] * m[15] -
            m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] +
            m[12] * m[7] * m[10];

        inv.m[8] = m[4] * m[9] * m[15] -
            m[4] * m[11] * m[13] -
            m[8] * m[5] * m[15] +
            m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] -
            m[12] * m[7] * m[9];

        inv.m[12] = -m[4] * m[9] * m[14] +
            m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] +
            m[12] * m[6] * m[9];

        inv.m[1] = -m[1] * m[10] * m[15] +
            m[1] * m[11] * m[14] +
            m[9] * m[2] * m[15] -
            m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] +
            m[13] * m[3] * m[10];

        inv.m[5] = m[0] * m[10] * m[15] -
            m[0] * m[11] * m[14] -
            m[8] * m[2] * m[15] +
            m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] -
            m[12] * m[3] * m[10];

        inv.m[9] = -m[0] * m[9] * m[15] +
            m[0] * m[11] * m[13] +
            m[8] * m[1] * m[15] -
            m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] +
            m[12] * m[3] * m[9];

        inv.m[13] = m[0] * m[9] * m[14] -
            m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] -
            m[12] * m[2] * m[9];

        inv.m[2] = m[1] * m[6] * m[15] -
            m[1] * m[7] * m[14] -
            m[5] * m[2] * m[15] +
            m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] -
            m[13] * m[3] * m[6];

        inv.m[6] = -m[0] * m[6] * m[15] +
            m[0] * m[7] * m[14] +
            m[4] * m[2] * m[15] -
            m[4] * m[3] * m[14] -
            m[12] * m[2] * m[7] +
            m[12] * m[3] * m[6];

        inv.m[10] = m[0] * m[5] * m[15] -
            m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] -
            m[12] * m[3] * m[5];

        inv.m[14] = -m[0] * m[5] * m[14] +
            m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] +
            m[12] * m[2] * m[5];

        inv.m[3] = -m[1] * m[6] * m[11] +
            m[1] * m[7] * m[10] +
            m[5] * m[2] * m[11] -
            m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] +
            m[9] * m[3] * m[6];

        inv.m[7] = m[0] * m[6] * m[11] -
            m[0] * m[7] * m[10] -
            m[4] * m[2] * m[11] +
            m[4] * m[3] * m[10] +
            m[8] * m[2] * m[7] -
            m[8] * m[3] * m[6];

        inv.m[11] = -m[0] * m[5] * m[11] +
            m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] +
            m[8] * m[3] * m[5];

        inv.m[15] = m[0] * m[5] * m[10] -
            m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] -
            m[8] * m[2] * m[5];

        T det = m[0] * inv.m[0] + m[1] * inv.m[4] + m[2] * inv.m[8] + m[3] * inv.m[12];

        if (fabs(det) < 0.00001)
        {
            return inv;
        }

        det = T(1.0) / det;

        for (int i = 0; i < 16; i++)
        {
            inv.m[i] *= det;
        }

        return inv;
    }

    void FromQuaternion(const Point4f& q)
    {
        m[0] = 1 - 2 * q.y*q.y - 2 * q.z * q.z;
        m[1] = 2 * q.x*q.y + 2 * q.z*q.w;
        m[2] = 2 * q.x*q.z - 2 * q.y*q.w;

        m[4] = 2 * q.x*q.y - 2 * q.z*q.w;
        m[5] = 1 - 2 * q.x * q.x - 2 * q.z * q.z;
        m[6] = 2 * q.y*q.z + 2 * q.x*q.w;

        m[8] = 2 * q.x*q.z + 2 * q.y*q.w;
        m[9] = 2 * q.y*q.z - 2 * q.x*q.w;
        m[10] = 1 - 2 * q.x*q.x - 2 * q.y * q.y;
    }

    T m[16];
};

using Matrix4f = Matrix4<float>;
using float4x4 = Matrix4f;
