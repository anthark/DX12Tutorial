#ifndef _SHADOWS_H
#define _SHADOWS_H

#include "ShaderCommon.h"
#include "Light.h"
#include "Object.h"

Texture2DArray ShadowMap : register(t4);

SamplerState ShadowMapSampler : register(s3);
SamplerComparisonState ShadowMapSamplerCmp : register(s4);

float SampleShadow(in int lightIdx, in float3 worldPos, in int splitIdx)
{
    float4x4 worldToLight = lights[lightIdx].worldToLight[splitIdx];
    
	float4 lightSpacePos = mul(worldToLight, float4(worldPos, 1.0));
	
	bool pcf = (intSceneParams.y & RENDER_FLAG_PCF_ON) != 0;
	
	if (pcf)
	{
		return ShadowMap.SampleCmp(ShadowMapSamplerCmp, float3(lightSpacePos.xy, splitIdx), lightSpacePos.z);
	}
	else
	{
		float shadowDepth = ShadowMap.Sample(ShadowMapSampler, float3(lightSpacePos.xy, splitIdx)).x;
		float sampleDepth = lightSpacePos.z;

		return shadowDepth < sampleDepth ? 0.0 : 1.0;
	}
}

float CalculateShadowSimple(in int lightIdx, in float3 worldPos)
{
    return SampleShadow(lightIdx, worldPos, 0);
}

float CalculateShadowPSSM(in int lightIdx, in float3 worldPos)
{
    float res = 0;
    
    float3 cameraWorldPos = cameraWorldPosNear.xyz;
    float3 cameraWorldDir = cameraWorldDirFar.xyz;
    
    float dist = dot(worldPos - cameraWorldPos, cameraWorldDir);
    if (dist < shadowSplitDists.x)
    {
        res = SampleShadow(lightIdx, worldPos, 0);
    }
    else if (dist < shadowSplitDists.y)
    {
        res = SampleShadow(lightIdx, worldPos, 1);
    }
    else if (dist < shadowSplitDists.z)
    {
        res = SampleShadow(lightIdx, worldPos, 2);
    }
    else if (dist < shadowSplitDists.w)
    {
        res = SampleShadow(lightIdx, worldPos, 3);
    }
    else
    {
        res = 1.0;
    }
    
    return res;
}

bool IsInside(in float2 uv)
{
    return uv.x >= 0 && uv.x <= 1.0 && uv.y >= 0 && uv.y <= 1.0;
}

float CalculateShadowCSM(in int lightIdx, in float3 worldPos)
{
    float4x4 worldToLight = lights[lightIdx].worldToLight[0];
    float4 lightSpacePos = mul(worldToLight, float4(worldPos, 1.0));

    if (IsInside(lightSpacePos.xy))
    {
        return SampleShadow(lightIdx, worldPos, 0);
    }

    lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.y + float2(0.5,0.5);
    if (IsInside(lightSpacePos.xy))
    {
        return SampleShadow(lightIdx, worldPos, 1);
    }

    lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.z + float2(0.5,0.5);
    if (IsInside(lightSpacePos.xy))
    {
        return SampleShadow(lightIdx, worldPos, 2);
    }

    lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.w + float2(0.5,0.5);
    return SampleShadow(lightIdx, worldPos, 3);
}

float CalculateShadow(in int lightIdx, in float3 worldPos)
{
    int shadowMode = intSceneParams.z;
    
    if (shadowMode == SHADOW_MODE_PSSM)
    {
        return CalculateShadowPSSM(lightIdx, worldPos);
    }
    else if (shadowMode == SHADOW_MODE_CSM)
    {
        return CalculateShadowCSM(lightIdx, worldPos);
    }

    return CalculateShadowSimple(lightIdx, worldPos);
}

float3 GetSplitTint(in float3 worldPos)
{
    float3 res = float3(1,1,1);
    int shadowMode = intSceneParams.z;

    if ((intSceneParams.y & RENDER_FLAG_TINT_SPLITS) != 0)
    {
        if (shadowMode == SHADOW_MODE_PSSM)
        {
            float3 cameraWorldPos = cameraWorldPosNear.xyz;
            float3 cameraWorldDir = cameraWorldDirFar.xyz;
            
            float dist = dot(worldPos - cameraWorldPos, cameraWorldDir);
            if (dist < shadowSplitDists.x)
            {
                res = float3(2,1,1);
            }
            else if (dist < shadowSplitDists.y)
            {
                res = float3(1,2,1);
            }
            else if (dist < shadowSplitDists.z)
            {
                res = float3(1,1,2);
            }
            else if (dist < shadowSplitDists.w)
            {
                res = float3(2,2,1);
            }
        }
        else if (shadowMode == SHADOW_MODE_CSM)
        {
            float4x4 worldToLight = lights[0].worldToLight[0];
            float4 lightSpacePos = mul(worldToLight, float4(worldPos, 1.0));

            if (IsInside(lightSpacePos.xy))
            {
                return float3(2,1,1);
            }
            lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.y + float2(0.5,0.5);
            if (IsInside(lightSpacePos.xy))
            {
                return float3(1,2,1);
            }
            lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.z + float2(0.5,0.5);
            if (IsInside(lightSpacePos.xy))
            {
                return float3(1,1,2);
            }
            lightSpacePos.xy = (lightSpacePos.xy - float2(0.5,0.5))*lights[0].csmRatio.w + float2(0.5,0.5);
            return float3(2,1,2);
        }
    }

    return res;
}

float3 GetAreaTint(in float3 worldPos)
{
    int shadowMode = intSceneParams.z;
    bool tintArea = shadowMode == SHADOW_MODE_SIMPLE && (intSceneParams.y & RENDER_FLAG_TINT_OUT_AREA) != 0;
    
    float4x4 worldToLight = lights[0].worldToLight[0];

	float4 lightSpacePos = mul(worldToLight, float4(worldPos, 1.0));

    return tintArea && (lightSpacePos.x > 1.0 || lightSpacePos.x < 0.0 || lightSpacePos.y > 1.0 || lightSpacePos.y < 0.0) ?
        float3(1,1,2) : float3(1,1,1);
}

float3 GetTint(in float3 worldPos)
{
    return GetSplitTint(worldPos) * GetAreaTint(worldPos);
}

#endif // _SHADOWS_H
