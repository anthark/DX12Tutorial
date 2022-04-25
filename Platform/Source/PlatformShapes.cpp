#include "stdafx.h"
#include "PlatformShapes.h"

namespace
{

template<typename T>
T& GetValue(T* pStartValue, size_t stride, int idx)
{
    return *reinterpret_cast<T*>(reinterpret_cast<UINT8*>(pStartValue) + stride * idx);
}

}

namespace Platform
{

void GetCubeDataSize(bool color, bool uv, size_t& indexCount, size_t& vertexCount)
{
    indexCount = 36;

    if (!uv)
    {
        vertexCount = 8;
    }
    else
    {
        vertexCount = 24;
    }
}

void CreateCube(UINT16* pIndices, size_t stride, Point3f* pPos, Point3f* pColor, Point2f* pUV, Point3f* pNormal)
{
    if (pUV == nullptr)
    {

        GetValue(pPos, stride, 0) = Point3f{ -0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 1) = Point3f{ 0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 2) = Point3f{ 0.5f,  0.5f, -0.5f };
        GetValue(pPos, stride, 3) = Point3f{ -0.5f,  0.5f, -0.5f };
        GetValue(pPos, stride, 4) = Point3f{ -0.5f, -0.5f,  0.5f };
        GetValue(pPos, stride, 5) = Point3f{ 0.5f, -0.5f,  0.5f };
        GetValue(pPos, stride, 6) = Point3f{ 0.5f,  0.5f,  0.5f };
        GetValue(pPos, stride, 7) = Point3f{ -0.5f,  0.5f,  0.5f };

        static const UINT16 indices[36] = {
            0, 2, 1, 0, 3, 2,
            4, 1, 5, 4, 0, 1,
            1, 6, 5, 1, 2, 6,
            5, 7, 4, 5, 6, 7,
            4, 3, 0, 4, 7, 3,
            3, 6, 2, 3, 7, 6
        };

        memcpy(pIndices, indices, sizeof(indices));

        if (pColor != nullptr)
        {
           GetValue(pColor, stride, 0) = Point3f{0,0,0};
           GetValue(pColor, stride, 1) = Point3f{0,0,1};
           GetValue(pColor, stride, 2) = Point3f{0,1,0};
           GetValue(pColor, stride, 3) = Point3f{0,1,1};
           GetValue(pColor, stride, 4) = Point3f{1,0,0};
           GetValue(pColor, stride, 5) = Point3f{1,0,1};
           GetValue(pColor, stride, 6) = Point3f{1,1,0};
           GetValue(pColor, stride, 7) = Point3f{1,1,1};
        }
    }
    else
    {
        GetValue(pPos, stride, 0) = Point3f{ -0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 1) = Point3f{ 0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 2) = Point3f{ 0.5f,  0.5f, -0.5f };
        GetValue(pPos, stride, 3) = Point3f{ -0.5f,  0.5f, -0.5f };

        GetValue(pPos, stride, 4) = Point3f{ 0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 5) = Point3f{ 0.5f, -0.5f,  0.5f };
        GetValue(pPos, stride, 6) = Point3f{ 0.5f,  0.5f,  0.5f };
        GetValue(pPos, stride, 7) = Point3f{ 0.5f,  0.5f, -0.5f };

        GetValue(pPos, stride, 8) = Point3f{  0.5f, -0.5f, 0.5f };
        GetValue(pPos, stride, 9) = Point3f{ -0.5f, -0.5f, 0.5f };
        GetValue(pPos, stride, 10) = Point3f{ -0.5f, 0.5f, 0.5f };
        GetValue(pPos, stride, 11) = Point3f{  0.5f, 0.5f, 0.5f };

        GetValue(pPos, stride, 12) = Point3f{ -0.5f, -0.5f, 0.5f };
        GetValue(pPos, stride, 13) = Point3f{ -0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 14) = Point3f{ -0.5f,  0.5f, -0.5f };
        GetValue(pPos, stride, 15) = Point3f{ -0.5f,  0.5f, 0.5f };

        GetValue(pPos, stride, 16) = Point3f{ -0.5f, -0.5f,  0.5f };
        GetValue(pPos, stride, 17) = Point3f{  0.5f, -0.5f,  0.5f };
        GetValue(pPos, stride, 18) = Point3f{  0.5f, -0.5f, -0.5f };
        GetValue(pPos, stride, 19) = Point3f{ -0.5f, -0.5f, -0.5f };

        GetValue(pPos, stride, 20) = Point3f{ -0.5f, 0.5f, -0.5f };
        GetValue(pPos, stride, 21) = Point3f{  0.5f, 0.5f, -0.5f };
        GetValue(pPos, stride, 22) = Point3f{  0.5f, 0.5f,  0.5f };
        GetValue(pPos, stride, 23) = Point3f{ -0.5f, 0.5f,  0.5f };

        GetValue(pUV, stride, 0) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 1) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 2) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 3) = Point2f{ 0, 0 };

        GetValue(pUV, stride, 4) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 5) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 6) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 7) = Point2f{ 0, 0 };

        GetValue(pUV, stride, 8) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 9) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 10) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 11) = Point2f{ 0, 0 };

        GetValue(pUV, stride, 12) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 13) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 14) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 15) = Point2f{ 0, 0 };

        GetValue(pUV, stride, 16) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 17) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 18) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 19) = Point2f{ 0, 0 };

        GetValue(pUV, stride, 20) = Point2f{ 0, 1 };
        GetValue(pUV, stride, 21) = Point2f{ 1, 1 };
        GetValue(pUV, stride, 22) = Point2f{ 1, 0 };
        GetValue(pUV, stride, 23) = Point2f{ 0, 0 };

        if (pNormal != nullptr)
        {
            GetValue(pNormal, stride, 0) = Point3f{ 0, 0, -1 };
            GetValue(pNormal, stride, 1) = Point3f{ 0, 0, -1 };
            GetValue(pNormal, stride, 2) = Point3f{ 0, 0, -1 };
            GetValue(pNormal, stride, 3) = Point3f{ 0, 0, -1 };

            GetValue(pNormal, stride, 4) = Point3f{ 1, 0, 0 };
            GetValue(pNormal, stride, 5) = Point3f{ 1, 0, 0 };
            GetValue(pNormal, stride, 6) = Point3f{ 1, 0, 0 };
            GetValue(pNormal, stride, 7) = Point3f{ 1, 0, 0 };

            GetValue(pNormal, stride, 8) =  Point3f{ 0, 0, 1 };
            GetValue(pNormal, stride, 9) =  Point3f{ 0, 0, 1 };
            GetValue(pNormal, stride, 10) = Point3f{ 0, 0, 1 };
            GetValue(pNormal, stride, 11) = Point3f{ 0, 0, 1 };

            GetValue(pNormal, stride, 12) = Point3f{ -1, 0, 0 };
            GetValue(pNormal, stride, 13) = Point3f{ -1, 0, 0 };
            GetValue(pNormal, stride, 14) = Point3f{ -1, 0, 0 };
            GetValue(pNormal, stride, 15) = Point3f{ -1, 0, 0 };

            GetValue(pNormal, stride, 16) = Point3f{ 0, -1, 0 };
            GetValue(pNormal, stride, 17) = Point3f{ 0, -1, 0 };
            GetValue(pNormal, stride, 18) = Point3f{ 0, -1, 0 };
            GetValue(pNormal, stride, 19) = Point3f{ 0, -1, 0 };

            GetValue(pNormal, stride, 20) = Point3f{ 0, 1, 0 };
            GetValue(pNormal, stride, 21) = Point3f{ 0, 1, 0 };
            GetValue(pNormal, stride, 22) = Point3f{ 0, 1, 0 };
            GetValue(pNormal, stride, 23) = Point3f{ 0, 1, 0 };
        }

        static const UINT16 indices[36] = {
            0, 2, 1, 0, 3, 2,
            4, 6, 5, 4, 7, 6,
            8, 10, 9, 8, 11, 10,
            12, 14, 13, 12, 15, 14,
            16, 18, 17, 16, 19, 18,
            20, 22, 21, 20, 23, 22
        };

        memcpy(pIndices, indices, sizeof(indices));
    }
}

void GetGridDataSize(int gridCells, size_t& indexCount, size_t& vertexCount)
{
    indexCount = vertexCount = (gridCells + 1) * 4;
}

void CreateGrid(int gridCells, float gridCellSize, UINT16* pIndices, size_t stride, Point3f* pPos)
{
    for (int i = 0; i <= gridCells; i++)
    {
        GetValue(pPos, stride, i * 2 + 0) = Point3f{ (-gridCells / 2 + i) * gridCellSize, 0.0f, -gridCells / 2 * gridCellSize };
        GetValue(pPos, stride, i * 2 + 1) = Point3f{ (-gridCells / 2 + i) * gridCellSize, 0.0f,  gridCells / 2 * gridCellSize };
        GetValue(pPos, stride, (gridCells + 1) * 2 + i * 2 + 0) = Point3f{ -gridCells / 2 * gridCellSize, 0.0f, (-gridCells / 2 + i) * gridCellSize };
        GetValue(pPos, stride, (gridCells + 1) * 2 + i * 2 + 1) = Point3f{ gridCells / 2 * gridCellSize, 0.0f, (-gridCells / 2 + i) * gridCellSize };
    }
    for (int i = 0; i < (gridCells + 1) * 4; i++)
    {
        pIndices[i] = (UINT16)i;
    }
}

void GetSphereDataSize(size_t latCells, size_t lonCells, size_t& indexCount, size_t& vertexCount)
{
    vertexCount = (latCells + 1) * (lonCells + 1);
    indexCount = latCells * lonCells * 6;
}

void CreateSphere(size_t latCells, size_t lonCells, UINT16* pIndices, size_t stride, Point3f* pPos, Point3f* pNormal, Point2f* pUV)
{
    for (size_t lat = 0; lat < latCells + 1; lat++)
    {
        for (size_t lon = 0; lon < lonCells + 1; lon++)
        {
            int index = (int)(lat*(lonCells + 1) + lon);
            float lonAngle = 2.0f*(float)M_PI * lon / lonCells + (float)M_PI;
            float latAngle = -(float)M_PI / 2 + (float)M_PI * lat / latCells;

            Point3f r = Point3f{
                sinf(lonAngle)*cosf(latAngle),
                sinf(latAngle),
                cosf(lonAngle)*cosf(latAngle)
            };

            GetValue(pPos, stride, index) = r * 0.5f;
            if (pNormal != nullptr)
            {
                GetValue(pNormal, stride, index) = r;
            }
            if (pUV != nullptr)
            {
                GetValue(pUV, stride, index) = Point2f{ (float)lon / lonCells, 1.0f - (float)lat / latCells };
            }
        }
    }

    for (size_t lat = 0; lat < latCells; lat++)
    {
        for (size_t lon = 0; lon < lonCells; lon++)
        {
            size_t index = lat * lonCells * 6 + lon * 6;
            pIndices[index + 0] = (UINT16)(lat*(latCells + 1) + lon + 0);
            pIndices[index + 1] = (UINT16)(lat*(latCells + 1) + lon + 1);
            pIndices[index + 2] = (UINT16)(lat*(latCells + 1) + latCells + 1 + lon);
            pIndices[index + 3] = (UINT16)(lat*(latCells + 1) + lon + 1);
            pIndices[index + 4] = (UINT16)(lat*(latCells + 1) + latCells + 1 + lon + 1);
            pIndices[index + 5] = (UINT16)(lat*(latCells + 1) + latCells + 1 + lon);
        }
    }
}

} // Platform
