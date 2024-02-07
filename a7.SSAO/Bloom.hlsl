#include "Tonemap.h"

Texture2D HDRTexture : register(t32);
SamplerState NoMipSampler : register(s2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOut output;

    output.pos = float4(pos, 1.0);
    output.uv = uv;

    return output;
}

#ifdef DETECT

float Luminance(in float3 color)
{
    return log(0.2126*color.r + 0.7152*color.g + 0.0722*color.b + 1.0);
}

float4 PS(VSOut input) : SV_TARGET
{
    float4 color = HDRTexture.Sample(NoMipSampler, input.uv);
    
    //if (Luminance(color.rgb) < luminance * 5.0)
    if (Luminance(color.rgb) < 1)
    {
        color = float4(0,0,0,0);
    }

    return color;
}
#endif // DETECT

#ifdef GAUSS_BLUR_NAIVE

static const float weight2D[5*5] = {
    0.200607, 0.106969, 0.016013, 0.000653, 0.000007,
    0.106969, 0.057038, 0.008539, 0.000348, 0.000004,
    0.016013, 0.008539, 0.001278, 0.000052, 0.000001,
    0.000653, 0.000348, 0.000052, 0.000002, 0,
    0.000007, 0.000004, 0.000001, 0, 0
};

float4 PS(VSOut input) : SV_TARGET
{
    float2 uvOffset = 1.0 / imageSize;

    float4 result = float4(0,0,0,0);
    for (int i = -4; i <= 4; i++)
    {
        for (int j = -4; j <= 4; j++)
        {
            float2 uv = float2(input.uv.x + i * uvOffset.x, input.uv.y + j * uvOffset.y);

            result += weight2D[abs(i)*5 + abs(j)] * HDRTexture.Sample(NoMipSampler, uv);
        }
    }

    return result;
}

#endif // GAUSS_BLUR_NAIVE

static const float weight1D[5] = {0.447892, 0.238827, 0.035753, 0.001459, 0.000016};

#ifdef GAUSS_BLUR_SEPARATED_VERTICAL
float4 PS(VSOut input) : SV_TARGET
{
    float uvOffset = 1.0 / imageSize.y;

    float4 result = float4(0,0,0,0);
    for (int i = -4; i <= 4; i++)
    {
        float2 uv = float2(input.uv.x, input.uv.y + i * uvOffset);

        result += weight1D[abs(i)] * HDRTexture.Sample(NoMipSampler, uv);
    }

    return result;
}
#endif // GAUSS_BLUR_SEPARATED_VERTICAL

#ifdef GAUSS_BLUR_SEPARATED_HORIZONTAL
float4 PS(VSOut input) : SV_TARGET
{
    float uvOffset = 1.0 / imageSize.x;

    float4 result = float4(0,0,0,0);
    for (int i = -4; i <= 4; i++)
    {
        float2 uv = float2(input.uv.x + i * uvOffset, input.uv.y);

        result += weight1D[abs(i)] * HDRTexture.Sample(NoMipSampler, uv);
    }

    return result;
}
#endif // GAUSS_BLUR_SEPARATED_HORIZONTAL

#ifdef GAUSS_BLUR_COMPUTE

cbuffer ComputeGaussBlurCB : register (b0)
{
    int2 cgbImageSize;
}

RWTexture2D<float4> dstTexture : register(u0);

static const int BlurGroupSize = 64;
#ifdef GAUSS_BLUR_COMPUTE_SIZE_3
static const int BlurSize = 3;
#else
static const int BlurSize = 7;
#endif
static const int HalfBlurSize = (BlurSize - 1) / 2;

[numthreads( BlurGroupSize, 1, 1 )]
void CS( uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    float4 curSum = float4(0,0,0,0);

#ifdef GAUSS_BLUR_COMPUTE_HORZ
    int y = groupId.y * BlurGroupSize + threadId.x;
    if (y > cgbImageSize.y)
    {
        return;
    }

    for (int i = -HalfBlurSize; i <= HalfBlurSize; i++)
    {
        int2 pixCoord = clamp(int2(groupId.x * BlurGroupSize + i, groupId.y * BlurGroupSize + threadId.x), int2(0,0), cgbImageSize);
        curSum += HDRTexture[pixCoord];
    }

    for (int i = 0; i < BlurGroupSize; i++)
    {
        int2 pixCoord = int2(groupId.x * BlurGroupSize + i, groupId.y * BlurGroupSize + threadId.x);
        dstTexture[pixCoord] = curSum / BlurSize;

        int2 prevPixCoord = max(int2(groupId.x * BlurGroupSize + i - HalfBlurSize, groupId.y * BlurGroupSize + threadId.x), int2(0,0));
        int2 nextPixCoord = min(int2(groupId.x * BlurGroupSize + i + HalfBlurSize + 1, groupId.y * BlurGroupSize + threadId.x), cgbImageSize);

        curSum = curSum - HDRTexture[prevPixCoord] + HDRTexture[nextPixCoord];
    }
}
#endif // GAUSS_BLUR_COMPUTE_HORZ

#ifdef GAUSS_BLUR_COMPUTE_VERT
    int x = groupId.x * BlurGroupSize + threadId.x;
    if (x > cgbImageSize.x)
    {
        return;
    }

    for (int i = -HalfBlurSize; i <= HalfBlurSize; i++)
    {
        int2 pixCoord = clamp(int2(groupId.x * BlurGroupSize + threadId.x, groupId.y * BlurGroupSize + i), int2(0,0), cgbImageSize);
        curSum += HDRTexture[pixCoord];
    }

    for (int i = 0; i < BlurGroupSize; i++)
    {
        int2 pixCoord = int2(groupId.x * BlurGroupSize + threadId.x, groupId.y * BlurGroupSize + i);
        dstTexture[pixCoord] = curSum / BlurSize;

        int2 prevPixCoord = max(int2(groupId.x * BlurGroupSize + threadId.x, groupId.y * BlurGroupSize + i - HalfBlurSize), int2(0,0));
        int2 nextPixCoord = min(int2(groupId.x * BlurGroupSize + threadId.x, groupId.y * BlurGroupSize + i + HalfBlurSize + 1), cgbImageSize);

        curSum = curSum - HDRTexture[prevPixCoord] + HDRTexture[nextPixCoord];
    }
}
#endif // GAUSS_BLUR_COMPUTE_VERT

#endif // GAUSS_BLUR_COMPUTE
