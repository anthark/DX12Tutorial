#ifndef _OBJECT_H
#define _OBJECT_H

#include "ShaderCommon.h"

CONST_BUFFER(ObjectData, 2)
{
    float4x4 transform;
    float4x4 transformNormals;
    float4 pbr;             // x - roughness, y - metalness, z - dielectric F0, w - alpha cutoff
    float4 pbr2;            // x = occlusion strength
    float4 metalF0;
    float4 emissiveFactor;  // Emissive factor
    int4   nodeIndex;       // x - node index (when no skinning is used)
};

#endif // _OBJECT_H
