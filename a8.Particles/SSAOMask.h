#ifndef _SSAOMASK_H
#define _SSAOMASK_H

#include "ShaderCommon.h"

CONST_BUFFER(SSAOMaskParams, 2)
{
    float4x4 invVP;
    int4 ssaoSampleCount;               // x - count of samples in kernel, y - noise size

    float ssaoParams;                   // Kernel size in world units
    int ssaoMode;                       // SSAO mode
    int ssaoNoiseSize;                  // Noise size
    int ssaoUseRange;                   // Use range cutoff

    float4 ssaoSamples[512];            // Kernel samples
    float4 ssaoNoise[256];              // Noise values
};

#endif // _SSAOMASK_H