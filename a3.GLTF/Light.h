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

struct LightValue
{
    float3 color;       // Light color (including intensity)
    float3 lightDir;    // Direction to light from current world position
    float attenuation;  // Light attenuation at given point
};

LightValue CalculateLight(in float3 worldPos, unsigned int lightIndex)
{
    LightValue res;

    res.lightDir = lights[lightIndex].dir.xyz;
    res.attenuation = 1.0;
    res.color = lights[lightIndex].color.xyz;

    // Point and spot lights
    if (lights[lightIndex].type == LT_Point || lights[lightIndex].type == LT_Spot)
    {
        res.lightDir = worldPos - lights[lightIndex].pos;
        float dirLen = max(0.0001, length(res.lightDir));

        res.attenuation = clamp(1.0 / (dirLen * dirLen), 0, 1.0);

        res.lightDir = res.lightDir / dirLen;
    }

    // Spot light - apply falloff angles
    if (lights[lightIndex].type == LT_Spot)
    {
        float angle = acos(dot(res.lightDir, lights[lightIndex].dir.xyz));
        res.attenuation *= saturate(lerp(1, 0, (angle - lights[lightIndex].falloffAngles.x) / lights[lightIndex].falloffAngles.y));
    }

    return res;
}

#endif // !__cplusplus

#endif // !_LIGHTS_H
