#ifndef _LIGHT_GEOMETRY_H
#define _LIGHT_GEOMETRY_H

#include "ShaderCommon.h"

CONST_BUFFER(LightGeometryData, 2)
{
    int4 lightIndex;     // x - light index
    float4x4 lightTransform;
};

#endif // _LIGHT_GEOMETRY_H
