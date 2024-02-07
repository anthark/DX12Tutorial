#ifndef _LIGHTGRID_H
#define _LIGHTGRID_H

static const int LightgridCellSize = 16;

CONST_BUFFER(LightsCull, 0)
{
    float4x4 cullInverseProj;       // Inverse projection matrix
    float4x4 cullV;                 // Camera View matrix
    unsigned int lightCullCount;    // Number of active lights
    unsigned int lightgridCellsX;   // Width of lightgrid in cells
    float2 padding0;                // Padding as lights array starts at 16-byte aligned address
    float4 lightsCull[MaxLights];   // We only cull spot lights, so xyz - is position, w - is radius
};

struct LightgridCell
{
    float4 planes[4];
};

#endif // _LIGHTGRID_H
