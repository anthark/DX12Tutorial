#ifndef _CUBEMAP_TEST_GEOM_H
#define _CUBEMAP_TEST_GEOM_H

#include "ShaderCommon.h"

CONST_BUFFER(CubemapTestGeomData, 2)
{
	float4x4 transform;
	float4 cubeParams; // xyz - pos, w - cubemap index
};

#endif // _CUBEMAP_TEST_GEOM_H
