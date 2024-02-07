#ifndef _LIGHTS_H
#define _LIGHTS_H

#include "ShaderCommon.h"

static const unsigned int MaxLights = 32;

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
    float4x4 worldToLight[4];  // Converting to light coordinate system
    float4 csmRatio;        // To step from light to light in CSM shadow
    float4 cutoffDist;      // Cutoff by attenuation threshold distance
};

CONST_BUFFER(Lights, 1)
{
    unsigned int lightCount;
    float3 ambientColor;
    float4 lightCutoff;

    Light lights[MaxLights];
};

#ifndef __cplusplus

struct LightValue
{
    float3 color;       // Light color (including intensity)
    float3 lightDir;    // Direction to light from current world position
    float attenuation;  // Light attenuation at given point
    bool shadow;        // Light has shadow
    bool tooFar;        // Light is too far and doesn't contribute
};

LightValue CalculateLight(in float3 worldPos, unsigned int lightIndex)
{
    LightValue res;

    res.lightDir = normalize(lights[lightIndex].dir.xyz);
    res.attenuation = 1.0;
    res.color = lights[lightIndex].color.xyz;
    res.tooFar = false;

    if (lights[lightIndex].type == LT_Point)
    {
        float distSqr = (worldPos.x - lights[lightIndex].pos.x)*(worldPos.x - lights[lightIndex].pos.x)
            + (worldPos.y - lights[lightIndex].pos.y)*(worldPos.y - lights[lightIndex].pos.y)
            + (worldPos.z - lights[lightIndex].pos.z)*(worldPos.z - lights[lightIndex].pos.z);
        res.attenuation *= 1.0 - smoothstep(lights[lightIndex].cutoffDist.x, 1.21*lights[lightIndex].cutoffDist.x, distSqr);
        if (distSqr > 1.21*lights[lightIndex].cutoffDist.x)
        {
            res.tooFar = true;
            return res;
        }
    }

    // Point and spot lights
    if (lights[lightIndex].type == LT_Point || lights[lightIndex].type == LT_Spot)
    {
        res.lightDir = worldPos - lights[lightIndex].pos;
        float dirLen = max(0.0001, length(res.lightDir));

        res.attenuation *= clamp(1.0 / (dirLen * dirLen), 0, 1.0);

        res.lightDir = res.lightDir / dirLen;
    }

    // Spot light - apply falloff angles
    if (lights[lightIndex].type == LT_Spot)
    {
        float angle = acos(dot(res.lightDir, lights[lightIndex].dir.xyz));
        res.attenuation *= saturate(lerp(1, 0, (angle - lights[lightIndex].falloffAngles.x) / lights[lightIndex].falloffAngles.y));
    }

    res.shadow = lights[lightIndex].type == LT_Direction;

    return res;
}

#endif // !__cplusplus

#endif // !_LIGHTS_H
