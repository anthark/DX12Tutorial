#include "Object.h"

Texture2D DiffuseTexture : register(t7);

SamplerState MinMagMipLinear : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

int JointIndex(int idx)
{
    int4 idxValues = jointIndices[idx / 4];

    switch (idx % 4)
    {
    case 0:
        return idxValues.x;
        break;
    case 1:
        return idxValues.y;
        break;
    case 2:
        return idxValues.z;
        break;
    case 3:
        return idxValues.w;
        break;
    }

    return 0;
}

VSOut VS(
    float3 pos : POSITION
    , float3 normal : NORMAL
    , float2 uv : TEXCOORD
#ifdef SKINNED
    , uint4 joints : TEXCOORD1
    , float4 weights : TEXCOORD2
#endif // SKINNED
)
{
    VSOut output;

#ifdef SKINNED
    int4 jointIdx = int4(
        JointIndex(joints.x),
        JointIndex(joints.y),
        JointIndex(joints.z),
        JointIndex(joints.w)
        );

    float4x4 _transform = nodeTransforms[jointIdx.x] * weights.x
        + nodeTransforms[jointIdx.y] * weights.y
        + nodeTransforms[jointIdx.z] * weights.z
        + nodeTransforms[jointIdx.w] * weights.w;
#else
    float4x4 _transform = nodeTransforms[nodeIndex.x];
#endif // !SKINNED

    float4 worldPos = mul(modelTransform, mul(_transform, float4(pos, 1.0)));
    output.pos = mul(VP, worldPos);
    output.uv = uv;

    return output;
}

#ifdef KHR_SPECGLOSS

void PS(VSOut input)
{
    #ifdef PLAIN_COLOR
        float4 Cdiff = ksgDiffFactor;
    #else
        float4 Cdiff = DiffuseTexture.Sample(MinMagMipLinear, input.uv) * ksgDiffFactor;
    #endif

    #ifdef ALPHA_KILL
    float alpha = Cdiff.a;
    if (alpha < pbr.w)
    {
        discard;
    }
    #endif // ALPHA_KILL
}

#else

void PS(VSOut input)
{
    #ifdef PLAIN_COLOR
        float4 F0 = metalF0;
    #else
        float4 diffuse  = DiffuseTexture.Sample(MinMagMipLinear, input.uv);
        float4 F0 = diffuse * metalF0;
    #endif

    #ifdef ALPHA_KILL
    float alpha = F0.a;
    if (alpha < pbr.w)
    {
        discard;
    }
    #endif // ALPHA_KILL
}

#endif // !KHR_SPECGLOSS

