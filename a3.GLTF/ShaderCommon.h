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
#define RENDER_MODE_NORMAL 10

#define MAX_NODES 200

CONST_BUFFER(SceneCommon, 0)
{
    float4x4 VP;
    float4 cameraPos;
    float4 sceneParams;     // x - exposure, y - bloom threshold
    int4 intSceneParams;    // x - render mode
    float4 imageSize;

    float4x4 nodeTransform[MAX_NODES];
    float4x4 nodeNormalTransform[MAX_NODES];
    int4 jointIndices[MAX_NODES];
};

#endif // !_SHADERCOMMON_H
