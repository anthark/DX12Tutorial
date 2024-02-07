#include "Luminance.h"

RWTexture2D<float4> dstTexture : register(u0);
Texture2D<float4> srcTexture : register(t0);

groupshared float2 values[LuminanceGroupSize][LuminanceGroupSize];

float Luminance(in float3 color)
{
    return log(0.2126*color.r + 0.7152*color.g + 0.0722*color.b + 1.0);
}

[numthreads( LuminanceGroupSize, LuminanceGroupSize, 1 )]
void CS( uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID )
{
    int LuminanceTileSize = LuminanceGroupSize * 2;
    int actualWidth = (groupId.x + 1) * LuminanceTileSize > srcTextureSize.x ? srcTextureSize.x % LuminanceTileSize : LuminanceTileSize;
    int actualHeight = (groupId.y + 1) * LuminanceTileSize > srcTextureSize.y ? srcTextureSize.y % LuminanceTileSize : LuminanceTileSize;

    int x = threadId.x * 2;
    int y = threadId.y * 2;

    {
        int count = 0;
        float value = 0;
        float maxValue;
        for (int i = 0; i < 2; i++)
        {
            for (int j = 0; j < 2; j++)
            {
                if (x + i < actualWidth && y + j < actualHeight)
                {
                    float4 v = srcTexture[int2(groupId.x * LuminanceTileSize + x + i, groupId.y * LuminanceTileSize + y + j)];
                    if (step0 != 0)
                    {
                        value += Luminance(v.xyz);
                        maxValue = max(maxValue, max(v.x, max(v.y, v.z)));
                    }
                    else
                    {
                        value += v.x;
                        maxValue = max(maxValue, v.y);
                    }
                    ++count;
                }
            }
        }
        values[threadId.x][threadId.y] = float2( count > 0 ? value / count : 0.0, maxValue);
    }

    GroupMemoryBarrierWithGroupSync();

    int reducedGroupSize = LuminanceGroupSize / 2;
    actualWidth = actualWidth % 2 == 0 ? actualWidth / 2 : actualWidth / 2 + 1;
    actualHeight = actualHeight % 2 == 0 ? actualHeight / 2 : actualHeight / 2 + 1;

    for (int k = 0; k < LuminanceLevels; k++)
    {
        int count = 0;
        float value = 0;
        float maxValue = 0;
        if (threadId.x < reducedGroupSize || threadId.y < reducedGroupSize)
        {
            for (int i = 0; i < 2; i++)
            {
                for (int j = 0; j < 2; j++)
                {
                    if (threadId.x * 2 + i < actualWidth && threadId.y * 2 + j < actualHeight)
                    {
                        float v = values[threadId.x * 2 + i][threadId.y * 2 + j].x;
                        value += v;
                        ++count;

                        maxValue = max(maxValue, values[threadId.x * 2 + i][threadId.y * 2 + j].y);
                    }
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();

        values[threadId.x][threadId.y] = float2(count > 0 ? value / count : 0.0, maxValue);

        GroupMemoryBarrierWithGroupSync();

        reducedGroupSize = reducedGroupSize / 2;
        actualWidth = actualWidth % 2 == 0 ? actualWidth / 2 : actualWidth / 2 + 1;
        actualHeight = actualHeight % 2 == 0 ? actualHeight / 2 : actualHeight / 2 + 1;
    }

    if (threadId.x == 0 && threadId.y == 0)
    {
        dstTexture[groupId.xy] = float4(values[threadId.x][threadId.y], 0, 0);
    }
}