#ifndef _SHADERCOMMON_H
#define _SHADERCOMMON_H

#ifdef __cplusplus
// C++ code
#define CONST_BUFFER(Name, Register) struct Name
#else
// HLSL code
#define CONST_BUFFER(Name, Register) cbuffer Name : register (b##Register)
#endif // __cplusplus

#define TONEMAP_MODE_NONE 0
#define TONEMAP_MODE_NORMALIZE 1
#define TONEMAP_MODE_REINHARD_SIMPLE 2
#define TONEMAP_MODE_UNCHARTED2 3
#define TONEMAP_MODE_UNCHARTED2_SRGB 4
#define TONEMAP_MODE_UNCHARTED2_SRGB_EYE_ADAPTATION 5

CONST_BUFFER(SceneCommon, 0)
{
    float4x4 VP;
    int4 tonemapMode; // x - tonemap mode
};

#endif // !_SHADERCOMMON_H