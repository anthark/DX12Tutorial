#ifndef _LUMINANCE_FINAL_H
#define _LUMINANCE_FINAL_H

#include "ShaderCommon.h"

CONST_BUFFER(LuminanceFinalParams, 0)
{
    float4 time; // x - delta in seconds
};

#endif // _LUMINANCE_FINAL_H
