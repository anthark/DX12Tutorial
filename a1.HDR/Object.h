#ifndef _OBJECT_H
#define _OBJECT_H

#include "ShaderCommon.h"

CONST_BUFFER(ObjectData, 2)
{
    float4x4 transform;
    float4x4 transformNormals;
};

#endif // _OBJECT_H
