#ifndef _LUMINANCE_H
#define _LUMINANCE_H

#include "ShaderCommon.h"

static const int LuminanceLevels = 5;
static const int LuminanceGroupSize = (int)pow(2,LuminanceLevels);

CONST_BUFFER(LuminanceParams, 0)
{
    int2 srcTextureSize;
    int  step0;
};

#endif // _LUMINANCE_H
