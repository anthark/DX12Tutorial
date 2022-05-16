#ifndef _OBJECT_H
#define _OBJECT_H

#include "ShaderCommon.h"

CONST_BUFFER(ObjectData, 2)
{
    float4x4 transform;
    float4x4 transformNormals;
    float4 pbr;             // x - roughness, y - metalness, z - dielectric F0
    float4 metalF0;
};

#endif // _OBJECT_H
