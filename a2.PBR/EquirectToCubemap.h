#ifndef _EQUIRECT_TO_CUBEMAP_H
#define _EQUIRECT_TO_CUBEMAP_H

#include "ShaderCommon.h"

CONST_BUFFER(EquirectToCubemapData, 2)
{
    float4x4 FaceVP;
    float4x4 transform;
    float roughness;
};

#ifndef __cplusplus

float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x) * 0.1591, asin(v.y) * 0.3183);
    uv.y += 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

#endif

#endif // _EQUIRECT_TO_CUBEMAP_H