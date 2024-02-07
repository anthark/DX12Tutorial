#include "ShaderCommon.h"

#include "Light.h"
#include "Lightgrid.h"

static const int MaxLightCullIterations = (MaxLights + LightgridCellSize*LightgridCellSize - 1) / (LightgridCellSize * LightgridCellSize);
static const int MaxLightsPerTile = 256;

RWTexture2D<float2> dstTexture : register(u0);
Texture2D<float> srcTexture : register(t0);
StructuredBuffer<LightgridCell> lightgridCells : register(t1);

RWStructuredBuffer<uint> lightIndexList : register (u1);
RWTexture2D<uint4> lightGrid : register(u2);

groupshared uint uintMaxDepth;
groupshared uint uintMinDepth;
groupshared uint opaqueLightsCount;
groupshared uint transLightsCount;

groupshared uint opaqueLights[MaxLightsPerTile];
groupshared uint transLights[MaxLightsPerTile];

[numthreads( LightgridCellSize, LightgridCellSize, 1 )]
void CS( uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID )
{
    // Initial setup
    if (threadId.x == 0 && threadId.y == 0)
    {
        uintMaxDepth = 0;
        uintMinDepth = 0xffffffff;

        opaqueLightsCount = 0;
        transLightsCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint x = groupId.x * LightgridCellSize + threadId.x;
    uint y = groupId.y * LightgridCellSize + threadId.y;

    uint width, height, mips;
    srcTexture.GetDimensions(0, width, height, mips);

    // Calculate min/max depth for given tile
    if (x < width && y < height)
    {
        float depth = srcTexture.Load(uint3(x,y,0));
        uint  uintDepth = asuint(depth);

        InterlockedMin(uintMinDepth, uintDepth);
        InterlockedMax(uintMaxDepth, uintDepth);
    }
    GroupMemoryBarrierWithGroupSync();

    // Linearize depth
    float minDepth = 0.0;
    float maxDepth = 0.0;
    {
        float4 ndc = float4(0, 0, asfloat(uintMinDepth), 1);
        float4 homoViewPos = mul(cullInverseProj, ndc);
        minDepth = homoViewPos.z / homoViewPos.w;
    }
    {
        float4 ndc = float4(0, 0, asfloat(uintMaxDepth), 1);
        float4 homoViewPos = mul(cullInverseProj, ndc);
        maxDepth = homoViewPos.z / homoViewPos.w;
    }

    // Perform light culling
    const LightgridCell lightgridCell = lightgridCells[groupId.y*lightgridCellsX + groupId.x];

    int threadIdx = threadId.y * LightgridCellSize + threadId.x;
    for (int i = 0; i < MaxLightCullIterations; i++)
    {
        int lightIndex = i * LightgridCellSize*LightgridCellSize + threadIdx;
        lightIndex++; // Assume first one is not for culling
        if (lightIndex >= lightCullCount)
        {
            break;
        }

        float3 pos = mul(cullV, float4(lightsCull[lightIndex].xyz, 1)).xyz;
        float radius = lightsCull[lightIndex].w;

        bool outside = false;
        // Check planes
        for (int j = 0; j < 4 && !outside; j++)
        {
            float d = dot(lightgridCell.planes[j], float4(pos, 1));
            if (d > radius)
            {
                outside = true;
            }
        }
        // Check local far plane
        if (!outside)
        {
            float depthEdge = maxDepth + radius;

            outside = pos.z > depthEdge;
            // Submit for transparent
            if (!outside)
            {
                uint index = 0;
                InterlockedAdd(transLightsCount, 1, index);
                if (index < MaxLightsPerTile)
                {
                    transLights[index] = lightIndex;
                }
            }
        }
        // Check for local near plane
        if (!outside)
        {
            float depthEdge = minDepth - radius;

            outside = pos.z < depthEdge;
            // Submit for opaque
            if (!outside)
            {
                uint index = 0;
                InterlockedAdd(opaqueLightsCount, 1, index);
                if (index < MaxLightsPerTile)
                {
                    opaqueLights[index] = lightIndex;
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (threadId.x == 0 && threadId.y == 0)
    {
        uint transStartIndex = 0;
        uint opaqueStartIndex = 0;

        if (transLightsCount > 0)
        {
            transStartIndex = 0;
            InterlockedAdd(lightIndexList[0], transLightsCount, transStartIndex);

            for (uint i = 0; i < transLightsCount; i++)
            {
                lightIndexList[transStartIndex + i] = transLights[i];
            }
        }
        if (opaqueLightsCount > 0)
        {
            uint startIndex = 0;
            InterlockedAdd(lightIndexList[0], opaqueLightsCount, opaqueStartIndex);

            for (uint i = 0; i < opaqueLightsCount; i++)
            {
                lightIndexList[opaqueStartIndex + i] = opaqueLights[i];
            }
        }

        // Final fill for light grid
        lightGrid[uint2(groupId.x, groupId.y)] = uint4(
            transLightsCount, opaqueLightsCount, transStartIndex, opaqueStartIndex
        );
    }

    // Test - write number of non-culled lights
    if (x < width && y < height)
    {
        //dstTexture[uint2(x,y)] = float2((float)transLightsCount / lightCullCount, (float)opaqueLightsCount / lightCullCount);
    }
}