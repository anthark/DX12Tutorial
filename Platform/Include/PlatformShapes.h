#pragma once

#include "PlatformPoint.h"

namespace Platform
{

PLATFORM_API void GetCubeDataSize(bool color, bool uv, size_t& indexCount, size_t& vertexCount);
PLATFORM_API void CreateCube(UINT16* pIndices, size_t stride, Point3f* pPos, Point3f* pColor, Point2f* pUV = nullptr, Point3f* pNormal = nullptr);

PLATFORM_API void GetGridDataSize(int gridCells, size_t& indexCount, size_t& vertexCount);
PLATFORM_API void CreateGrid(int gridCells, float gridCellSize, UINT16* pIndices, size_t stride, Point3f* pPos);

PLATFORM_API void GetSphereDataSize(size_t latCells, size_t lonCells, size_t& indexCount, size_t& vertexCount);
PLATFORM_API void CreateSphere(size_t latCells, size_t lonCells, UINT16* pIndices, size_t stride, Point3f* pPos, Point3f* pNormal = nullptr, Point2f* pUV = nullptr);

} // Platform
