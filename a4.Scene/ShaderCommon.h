#ifndef _SHADERCOMMON_H
#define _SHADERCOMMON_H

#ifdef __cplusplus
// C++ code
#define CONST_BUFFER(Name, Register) struct Name
#else
// HLSL code
#define CONST_BUFFER(Name, Register) cbuffer Name : register (b##Register)

#define PI 3.14159265
#endif // __cplusplus

#define RENDER_MODE_LIGHTING 0
#define RENDER_MODE_DIFFUSE 1
#define RENDER_MODE_IBL_DIFFUSE 2
#define RENDER_MODE_SPECULAR 3
#define RENDER_MODE_SPEC_NORMAL_DISTRIBUTION 4
#define RENDER_MODE_SPEC_GEOMETRY 5
#define RENDER_MODE_SPEC_FRESNEL 6
#define RENDER_MODE_IBL_SPEC_ENV 7
#define RENDER_MODE_IBL_SPEC_FRESNEL 8
#define RENDER_MODE_IBL_SPEC_BRDF 9
#define RENDER_MODE_ALBEDO 10
#define RENDER_MODE_NORMALS 11

#define RENDER_FLAG_APPLY_BLOOM 0x0001
#define RENDER_FLAG_PCF_ON 0x0010
#define RENDER_FLAG_TINT_SPLITS 0x0100
#define RENDER_FLAG_TINT_OUT_AREA 0x1000

#define SHADOW_MODE_SIMPLE 0
#define SHADOW_MODE_PSSM 1
#define SHADOW_MODE_CSM 2

CONST_BUFFER(SceneCommon, 0)
{
    float4x4 VP;
    float4 cameraPos;

    float3 sceneParams;         // x - exposure, y - bloom threshold
    float bloomRatio;           // Bloom intensity ratio

    int3 intSceneParams;        // x - render mode
    int useSpecAA;              // Use specular anti-aliasing

    float4 imageSize;
    float4 cameraWorldPosNear;  // xyz - camera world pos, w - near
    float4 cameraWorldDirFar;   // xyz - camera world dir, w - far
    float4 shadowSplitDists;    // Shadow split distances
};

#endif // !_SHADERCOMMON_H
