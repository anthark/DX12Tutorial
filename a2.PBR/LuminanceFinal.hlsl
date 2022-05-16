#include "LuminanceFinal.h"

#include "Tonemap.h"

Texture2D<float4> srcTexture : register(t0);

struct Output
TONEMAP_PARAMS

RWStructuredBuffer<Output> output : register(u0);

[numthreads( 1, 1, 1 )]
void CS()
{
    uint width, height;
    srcTexture.GetDimensions(width, height);

    float value = 0;
    float maxValue = 0;
    for (uint j = 0; j < height; j++)
    {
        for (uint i = 0; i < width; i++)
        {
            //value = max(value, srcTexture[int2(i,j)]);
            value += srcTexture[int2(i,j)].r;
            maxValue = max(maxValue, srcTexture[int2(i,j)].g);
        }
    }
    value /= width * height;

    float averageLuminance = exp(value) - 1;
    float averageLuminanceAdapted = output[0].luminanceAdapted + (averageLuminance - output[0].luminanceAdapted) * (1.0 - exp(-time.x));

    float keyValue = 1.f - 2.f / (2.f + log10(averageLuminance + 1));
    float keyValueAdapted = 1.f - 2.f / (2.f + log10(averageLuminanceAdapted + 1));

    output[0].luminance = averageLuminance;
    output[0].luminanceAdapted = averageLuminanceAdapted;
    output[0].keyValue = keyValue;
    output[0].exposure = keyValue / averageLuminance;
    output[0].exposureAdapted = keyValue / averageLuminanceAdapted;
    output[0].maxValue = maxValue;
}