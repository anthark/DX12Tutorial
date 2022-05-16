#ifndef _LIGHTS_H
#define _LIGHTS_H

#include "ShaderCommon.h"

static const unsigned int MaxLights = 4;

static const unsigned int LT_None = 0;
static const unsigned int LT_Direction = 1;
static const unsigned int LT_Point = 2;
static const unsigned int LT_Spot = 3;

struct Light
{
    unsigned int type;
    float3 pos;             // For point and spot
    float4 dir;             // For spot and direction
    float4 color;           // For all
    float4 falloffAngles;   // For spot light only
};

CONST_BUFFER(Lights, 1)
{
    unsigned int lightCount;
    float3 ambientColor;

    Light lights[MaxLights];
};

#ifndef __cplusplus

float3 CalculateLight(in float3 pos, in float3 normal)
{
    float3 res;
    for (unsigned int i = 0; i < lightCount; i++)
    {
        // Diffuse part
        float3 dir = lights[i].dir;
        float attenuation = 1.0;

        // Point and spot lights
        if (lights[i].type == LT_Point || lights[i].type == LT_Spot)
        {
            dir = pos - lights[i].pos;
            float dirLen = max(0.0001, length(dir));

            //attenuation = clamp(1.0 / (1.0 + dirLen + dirLen * dirLen ), 0, 1.0);
            //attenuation = dirLen > 1.0 ? 0.0 : 1.0;
            attenuation = 1.0 - smoothstep(0.9, 1.0, dirLen);

            dir = dir / dirLen;
        }

        // Spot light - apply falloff angles
        if (lights[i].type == LT_Spot)
        {
            float angle = acos(dot(dir, lights[i].dir));
            attenuation *= saturate(lerp(1, 0, (angle - lights[i].falloffAngles.x) / lights[i].falloffAngles.y));
        }

        float3 lightColor = attenuation * (clamp(dot(-dir, normal), 0.0, 1.0) * lights[i].color.xyz);
        res += lightColor;
    }

    res += ambientColor;

    return res;
}

#endif // !__cplusplus

#endif // !_LIGHTS_H
