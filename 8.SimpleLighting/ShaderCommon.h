#ifndef _SHADERCOMMON_H
#define _SHADERCOMMON_H

#ifdef __cplusplus
// C++ code
#define CONST_BUFFER(Name, Register) struct Name
#else
// HLSL code
#define CONST_BUFFER(Name, Register) cbuffer Name : register (b##Register)
#endif // __cplusplus

CONST_BUFFER(Transform, 0)
{
    float4x4 VP;
};

#endif // !_SHADERCOMMON_H