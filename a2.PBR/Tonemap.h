#ifndef _TONEMAP_H
#define _TONEMAP_H

#include "ShaderCommon.h"

#define TONEMAP_PARAMS \
{\
    float luminance;\
    float luminanceAdapted;\
    float keyValue;\
    float exposure;\
    float exposureAdapted;\
    float maxValue;\
};

CONST_BUFFER(TonemapParams, 2)
TONEMAP_PARAMS

#endif // _TONEMAP_H
