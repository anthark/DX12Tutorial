#ifndef _PBRMATERIAL_H
#define _PBRMATERIAL_H

#include "ShaderCommon.h"

#include "Shadows.h"

#include "Lightgrid.h"

#ifndef __cplusplus

TextureCube DiffuseIrradianceMap : register(t0);
Texture2D BRDFLut : register(t1);
TextureCube EnvMap : register(t2);
// t3 is reserved for environment sphere cubemap
// t4 is reserved for shadowmap
#ifdef USE_LOCAL_CUBEMAPS
TextureCubeArray LocalIrradianceMapArray : register(t5);
TextureCubeArray LocalEnvironmentMapArray : register(t6);
#else
Texture2D<uint4> LightGrid : register(t5);
StructuredBuffer<uint> LightgridCells : register(t6);
#endif // !USE_LOCAL_CUBEMAPS

SamplerState MinMagMipLinear : register(s0);
SamplerState MinMagLinearMipPoint : register(s1);
SamplerState MinMagLinearMipPointBorder : register(s2);

#ifndef USE_LOCAL_CUBEMAPS
uint2 CalcLightGridCellCoord(float2 pixelCoord)
{
    return uint2(
        floor(pixelCoord.x / LightgridCellSize),
        floor(pixelCoord.y / LightgridCellSize)
    );
}

uint2 CalcLightIndices(float2 pixelCoord)
{
    uint2 cellCoord = CalcLightGridCellCoord(pixelCoord);

    uint4 cell = LightGrid[uint2(cellCoord.x, cellCoord.y)];

#ifdef TRANSPARENT
    return uint2(cell.z, cell.x);
#else
    return uint2(cell.w, cell.y);
#endif // !TRANSPARENT
}
#endif // !USE_LOCAL_CUBEMAPS

// Trowbridge-Reitz GGX
float NormalDistributionFunction(in float3 n, in float3 v, in float3 l, in float roughness)
{
    float3 halfVec = normalize(l + v);
    float ndoth = max(dot(halfVec, n), 0.0);
    float denom = (ndoth*ndoth*(roughness * roughness-1)+1);
    float ndf = roughness / denom;
    ndf *= ndf;
    ndf /= PI;
    return ndf;
}

// Schlick-GGX
float GeometryFunctionSub(in float3 n, in float3 r, float k)
{
    float ndotr = max(dot(n,r),0);
    return ndotr / (ndotr * (1 - k) + k);
}

// Smith's method
float GeometryFunction(in float3 n, in float3 v, in float3 l, float roughness)
{
    float k = (roughness + 1) * (roughness + 1) / 8;
    return GeometryFunctionSub(n, v, k) * GeometryFunctionSub(n, l, k);
}

// Fresnel-Schlick
float3 FresnelFunction(in float dF0, in float3 mF0, in float metalness, in float3 v, in float3 l)
{
    float3 F0 = lerp(float3(dF0, dF0, dF0), mF0, metalness);

    float3 halfVec = normalize(l + v);
    float hdotv = max(dot(halfVec, v), 0.0);

    return F0 + (float3(1.0,1.0,1.0) - F0) * pow(1.0 - hdotv, 5.0);
}

float3 FresnelSchlickRoughnessFunction(float3 F0, float roughness, in float3 v, in float3 n)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(max(1.0 - max(dot(v, n), 0.0), 0.0), 5.0);
}

//static const float3 LocalBBMin = float3(-5,-5,-5);
//static const float3 LocalBBMax = float3( 5, 5, 5);
static const float3 LocalBBMin = float3(-10,-10,-10);
static const float3 LocalBBMax = float3( 10, 10, 10);
static const float3 LocalBBCenter = float3(-14.3665848, 2.50000000, -5.12552881);

static const float3 LocalBBCenter2 = float3(-9.3665848, 2.50000000, -5.12552881);

static const float BlendRadius = 1.0;

float3 GetLocalCorrectedDir(in float3 worldPos, in float3 n, in float3 bbCenter, out float alpha)
{	
	float3 minT = -(LocalBBMin + bbCenter - worldPos)/n;
	float3 maxT = -(LocalBBMax + bbCenter - worldPos)/n;
	float3 T3 = max(minT, maxT);
	float T = min(min(T3.x, T3.y), T3.z);
	
	//alpha = max(1.0 - abs(bbCenter.x - worldPos.x)/LocalBBMax, 0);
	//alpha = abs(bbCenter.x - worldPos.x) > 2.5 ? 0 : 1;
	alpha = 1.0 - smoothstep(2.5 - BlendRadius, 2.5 + BlendRadius, abs(bbCenter.x - worldPos.x));
	
	float3 correctedPos = worldPos + T*n;
	
	return normalize(correctedPos - bbCenter);
}

void CalcLocalCubemaps(in float3 worldPos, out int4 idx)
{
	int localX = max((int)((worldPos.x - (localCubemapBasePosSize.x + localCubemapBasePosSize.w / 2))/localCubemapBasePosSize.w),0);
	int localY = max((int)((worldPos.z - (localCubemapBasePosSize.z + localCubemapBasePosSize.w / 2))/localCubemapBasePosSize.w),0);
	
	idx.x = localY * localCubemapGrid.x + localX;
	idx.y = localY * localCubemapGrid.x + min(localX + 1, localCubemapGrid.x - 1);
	idx.z = min(localY+1, localCubemapGrid.y - 1) * localCubemapGrid.x + localX;
	idx.w = min(localY+1, localCubemapGrid.y - 1) * localCubemapGrid.x + min(localX + 1, localCubemapGrid.x - 1);
}

float3 CalcCubemapCenter(in int idx)
{
	float size = localCubemapBasePosSize.w;
	return localCubemapBasePosSize.xyz + float3( size/2, size/2, size/2) + float3((idx % localCubemapGrid.x) * size, 0, (idx / localCubemapGrid.x) * size);
}

#ifdef USE_LOCAL_CUBEMAPS

float4 SampleLocalIrradiance(in float3 worldPos, in float3 n)
{
	int4 idx;
	CalcLocalCubemaps(worldPos, idx);
	
	float4 irradiance = float4(0,0,0,0);
	
	float alpha = 0;
	float4 localIrradiance = LocalIrradianceMapArray.Sample(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.x), alpha), idx.x));
	irradiance += localIrradiance * alpha;
	
	localIrradiance = LocalIrradianceMapArray.Sample(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.y), alpha), idx.y));
	irradiance += localIrradiance * alpha;
	
	float4 irradiance2 = float4(0,0,0,0);
	
	localIrradiance = LocalIrradianceMapArray.Sample(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.z), alpha), idx.z));
	irradiance2 += localIrradiance * alpha;
	
	localIrradiance = LocalIrradianceMapArray.Sample(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.w), alpha), idx.w));
	irradiance2 += localIrradiance * alpha;
	
	float alphaY0 = 1.0 - smoothstep(2.5 - BlendRadius, 2.5 + BlendRadius, abs(CalcCubemapCenter(idx.x).z - worldPos.z));
	float alphaY1 = 1.0 - smoothstep(2.5 - BlendRadius, 2.5 + BlendRadius, abs(CalcCubemapCenter(idx.z).z - worldPos.z));
	
	irradiance = irradiance * alphaY0 + irradiance2 * alphaY1;
	
	return irradiance;
}

float4 SampleLocalEnv(in float3 worldPos, in float3 n, float level)
{
	int4 idx;
	CalcLocalCubemaps(worldPos, idx);
	
	float4 env = float4(0,0,0,0);
	
	float alpha = 0;
	float4 localEnv = LocalEnvironmentMapArray.SampleLevel(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.x), alpha), idx.x), level);
	env += localEnv * alpha;
	
	localEnv = LocalEnvironmentMapArray.SampleLevel(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.y), alpha), idx.y), level);
	env += localEnv * alpha;
	
	float4 env2 = float4(0,0,0,0);
	localEnv = LocalEnvironmentMapArray.SampleLevel(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.z), alpha), idx.z), level);
	env2 += localEnv * alpha;
	
	localEnv = LocalEnvironmentMapArray.SampleLevel(MinMagMipLinear, float4(GetLocalCorrectedDir(worldPos, n, CalcCubemapCenter(idx.w), alpha), idx.w), level);
	env2 += localEnv * alpha;
	
	float alphaY0 = 1.0 - smoothstep(2.5 - BlendRadius, 2.5 + BlendRadius, abs(CalcCubemapCenter(idx.x).z - worldPos.z));
	float alphaY1 = 1.0 - smoothstep(2.5 - BlendRadius, 2.5 + BlendRadius, abs(CalcCubemapCenter(idx.z).z - worldPos.z));
	
	env = env * alphaY0 + env2 * alphaY1;
	
	env.rgb *= 5.0f;
	return env;
}

#endif // USE_LOCAL_CUBEMAPS

float3 SampleIrradiance(in float3 worldPos, in float3 n)
{
#ifdef USE_LOCAL_CUBEMAPS
	float4 localIrradiance = SampleLocalIrradiance(worldPos, n);
	return localIrradiance.rgb * localIrradiance.a + DiffuseIrradianceMap.Sample(MinMagMipLinear, n).rgb * (1.0 - localIrradiance.a);	
#else
	return DiffuseIrradianceMap.Sample(MinMagMipLinear, n).rgb;
#endif // !USE_LOCAL_CUBEMAPS
}

float3 SampleEnvironment(in float3 worldPos, in float3 n, in float level)
{
#ifdef USE_LOCAL_CUBEMAPS
	float4 localEnv = SampleLocalEnv(worldPos, n, level);
	return localEnv.rgb * localEnv.a + EnvMap.SampleLevel(MinMagMipLinear, n, level).rgb * (1.0 - localEnv.a);	
#else
	return EnvMap.SampleLevel(MinMagMipLinear, n, level).rgb;
#endif // !USE_LOCAL_CUBEMAPS
}

float SpecAAFilterRoughness(in float3 normal, in float3 l, in float3 v, in float roughness)
{
    float3 tangent = abs(normal.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 binormal = normalize(cross(normal, tangent));
    tangent = cross(binormal, normal);

    float3x3 tbn = transpose(float3x3(tangent, binormal, normal));

    float3 lh = normalize(mul(tbn,l) + mul(tbn,v));

    // Compute half-vector derivatives
    float2 hpp   = lh.xy/lh.z;
    float2 hppDx = ddx(hpp);
    float2 hppDy = ddy(hpp);

    // Compute filtering region
    float2 rectFp = (abs(hppDx) + abs(hppDy));
    float maxRectFp = max(rectFp.x, rectFp.y);

    float variance = min(maxRectFp * maxRectFp * 2.0, 0.18);

    return sqrt(roughness*roughness + variance); // Beckmann proxy convolution for GGX
}

float3 CalcPBRLight(in int i, in float3 worldPos, in float3 n, in float3 v, in float roughness, in float dielectricF0, in float3 metalF0, in float metalness)
{
    LightValue light = CalculateLight(worldPos, i);
    if (light.tooFar)
    {
        return float3(0,0,0);
    }
    float3 l = -light.lightDir;

    if (light.shadow)
    {
        light.color *= CalculateShadow(i, worldPos);
    }

    float3 radiance = light.color * light.attenuation;
    float ndotl = max(dot(n, l), 0.0);
    float NDF = NormalDistributionFunction(n, v, l, useSpecAA ? SpecAAFilterRoughness(n,l,v, roughness) : roughness );
    float G = GeometryFunction(n, v, l, roughness);
    float3 F = FresnelFunction(dielectricF0, metalF0.xyz, metalness, v, l);

    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metalness;

    float3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0);
    float3 specular     = numerator / max(denominator, 0.001);  

    // return outgoing radiance Lo
    return (kD * metalF0.xyz / PI + specular) * radiance * ndotl;
}

float3 CalcPBRAmbient(in float3 worldPos, in float3 r, in float roughness, in float dielectricF0, in float3 metalF0, in float metalness, in float3 n, in float3 v)
{
    static const float MAX_REFLECTION_LOD = 4.0;
    float3 prefilteredColor = SampleEnvironment(worldPos, r, roughness * MAX_REFLECTION_LOD);

    float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), metalF0.xyz, metalness);
    float3 F = FresnelSchlickRoughnessFunction(F0, roughness, v, n);

    float2 envBRDF  = BRDFLut.Sample(MinMagLinearMipPointBorder, float2(max(dot(n, v), 0.0), roughness)).rg;

    float3 specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metalness;
    float3 irradiance = SampleIrradiance(worldPos, n);
    float3 diffuse    = irradiance * metalF0.xyz;
    float3 ambient    = (kD * diffuse + specular);
    
    return ambient;
}

float4 CalcPBRColor(in float3 worldPos, in float3 n, in float3 v, in float roughness, in float metalness, in float dielectricF0, in float3 metalF0, in float2 pixelCoord)
{
    float4 color = float4(0,0,0,1);

    int renderMode = intSceneParams.x;
    bool receivesShadow = flags.x == 1;

    int lightsCulled = intSceneParams.w == 1;

    float3 r = reflect(-v, n);

#ifndef NO_POINT_LIGHTS
    if (lightsCulled)
    {
        uint2 lightIndices = CalcLightIndices(pixelCoord);
        color.xyz += CalcPBRLight(0, worldPos, n, v, roughness, dielectricF0, metalF0, metalness);
        [loop]
        for (uint i = 0; i < lightIndices.y; i++)
        {
            uint lightIdx = LightgridCells[lightIndices.x + i];
            color.xyz += CalcPBRLight(lightIdx, worldPos, n, v, roughness, dielectricF0, metalF0, metalness);
        }
    }
    else
    {
        for (unsigned int i = 0; i < lightCount; i++)
        {
            color.xyz += CalcPBRLight(i, worldPos, n, v, roughness, dielectricF0, metalF0, metalness);
        }
    }
#endif // !NO_POINT_LIGHTS

    // Final composition with ambient
    color.xyz += CalcPBRAmbient(worldPos, r, roughness, dielectricF0, metalF0, metalness, n, v);

    return color;
}

float3 CalcPBRLight_KHRSpecGloss(in int i, in float3 worldPos, in float3 n, in float3 v, in float roughness, in float3 inF0, in float3 Cdiff)
{
    LightValue light = CalculateLight(worldPos, i);
    if (light.tooFar)
    {
        return float3(0,0,0);
    }
    float3 l = -light.lightDir;

    if (light.shadow)
    {
        light.color *= CalculateShadow(i, worldPos);
    }

    float3 radiance = light.color * light.attenuation;
    float ndotl = max(dot(n, l), 0.0);

    float NDF = NormalDistributionFunction(n, v, l, useSpecAA ? SpecAAFilterRoughness(n,l,v, roughness) : roughness );
    float G = GeometryFunction(n, v, l, roughness);
    float3 F = FresnelFunction(0, inF0.xyz, 1.0, v, l);

    float3 kS = inF0;
    float3 kD = Cdiff * (1.0 - max(kS.x, max(kS.y, kS.z)));

    float3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0);
    float3 specular     = numerator / max(denominator, 0.001);  

    // add to outgoing radiance Lo
    return (kD / PI + specular) * radiance * ndotl;
}

float3 CalcPBRAmbient_KHRSpecGloss(in float3 worldPos, in float3 r, in float roughness, in float3 inF0, in float3 n, in float3 v, in float3 Cdiff)
{
    static const float MAX_REFLECTION_LOD = 4.0;
    float3 prefilteredColor = SampleEnvironment(worldPos, r, roughness * MAX_REFLECTION_LOD);

    float3 F0 = inF0;
    float3 F = FresnelSchlickRoughnessFunction(F0, roughness, v, n);
    float2 envBRDF  = BRDFLut.Sample(MinMagLinearMipPointBorder, float2(max(dot(n, v), 0.0), roughness)).rg;

    float3 specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

    float3 kS = F;
    float3 kD = Cdiff * (1.0 - max(kS.x, max(kS.y, kS.z)));
    float3 irradiance = SampleIrradiance(worldPos, n);
    float3 diffuse    = irradiance;
    float3 ambient    = (kD * diffuse + specular);

    return ambient;
}

float4 CalcPBRColor_KHRSpecGloss(in float3 worldPos, in float3 n, in float3 v, in float roughness, in float3 Cdiff, in float3 inF0, in float2 pixelCoord)
{
    float4 color = float4(0,0,0,1);

    int renderMode = intSceneParams.x;
    bool receivesShadow = flags.x == 1;

    int lightsCulled = intSceneParams.w == 1;

    float3 r = reflect(-v, n);

#ifndef NO_POINT_LIGHTS
    if (lightsCulled)
    {
        uint2 lightIndices = CalcLightIndices(pixelCoord);
        color.xyz += CalcPBRLight_KHRSpecGloss(0, worldPos, n, v, roughness, inF0, Cdiff);
        [loop]
        for (uint i = 0; i < lightIndices.y; i++)
        {
            uint lightIdx = LightgridCells[lightIndices.x + i];
            color.xyz += CalcPBRLight_KHRSpecGloss(lightIdx, worldPos, n, v, roughness, inF0, Cdiff);
        }
    }
    else
    {
        for (unsigned int i = 0; i < lightCount; i++)
        {
            color.xyz += CalcPBRLight_KHRSpecGloss(i, worldPos, n, v, roughness, inF0, Cdiff);
        }
    }
#endif // !NO_POINT_LIGHTS

    // Final composition with ambient color
    color.xyz += CalcPBRAmbient_KHRSpecGloss(worldPos, r, roughness, inF0, n, v, Cdiff);

    return color;
}

#endif // __cplusplus

#endif // _PBRMATERIAL_H
