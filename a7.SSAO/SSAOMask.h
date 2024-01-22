#ifndef _SSAOMASK_H
#define _SSAOMASK_H

#include "ShaderCommon.h"

CONST_BUFFER(SSAOMaskParams, 2)
{
    float4x4 invVP;
    int4 sampleCount;                   // x - count of samples in kernel
    float4 ssaoParams;                  // x - kernel size in world units
    float4 samples[512];                // Kernel samples
};

#endif // _SSAOMASK_H