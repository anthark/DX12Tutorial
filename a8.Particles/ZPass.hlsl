#include "Object.h"

Texture2D DiffuseTexture : register(t32);
#ifdef NORMAL_MAP
Texture2D NormalMapTexture : register(t34);
#endif // NORMAL_MAP

SamplerState MinMagMipLinear : register(s0);

#include "Directions.h"

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
#ifdef NORMAL_MAP
    float4 tangent : TANGENT;
#endif // NORMAL_MAP
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
#ifdef NORMAL_MAP
    , float4 tangent: TANGENT
#endif //  NORMAL_MAP
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
    float4x4 _transformNormals = nodeNormalTransforms[jointIdx.x] * weights.x
        + nodeNormalTransforms[jointIdx.y] * weights.y
        + nodeNormalTransforms[jointIdx.z] * weights.z
        + nodeNormalTransforms[jointIdx.w] * weights.w;
#else
    float4x4 _transform = nodeTransforms[nodeIndex.x];
    float4x4 _transformNormals = nodeNormalTransforms[nodeIndex.x];
#endif // !SKINNED

    float4 worldPos = mul(modelTransform, mul(_transform, float4(pos, 1.0)));
    output.pos = mul(VP, worldPos);
    output.normal = mul(modelNormalTransform, mul(_transformNormals, float4(normal, 1.0))).xyz;
    output.uv = uv;
#ifdef NORMAL_MAP
    output.tangent.xyz = mul(modelNormalTransform, mul(_transformNormals, float4(tangent.xyz, 1.0))).xyz;
    output.tangent.w = tangent.w;
#endif //  NORMAL_MAP

    return output;
}

#ifdef KHR_SPECGLOSS

float4 PS(VSOut input) : SV_Target0
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

#ifdef NORMAL_MAP
    Dirs d = CalcDirs(input.normal, input.uv, input.tangent, float4(0,0,0,0));
#else
    Dirs d = CalcDirs(input.normal, input.uv, float4(0,0,0,0), float4(0,0,0,0));
#endif

    return float4(d.n * 0.5 + float3(0.5, 0.5, 0.5), 1.0);
}

#else

float4 PS(VSOut input) : SV_Target0
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

#ifdef NORMAL_MAP
    Dirs d = CalcDirs(input.normal, input.uv, input.tangent, float4(0,0,0,0));
#else
    Dirs d = CalcDirs(input.normal, input.uv, float4(0,0,0,0), float4(0,0,0,0));
#endif

    return float4(d.n * 0.5 + float3(0.5, 0.5, 0.5), 1.0);
}

#endif // !KHR_SPECGLOSS

