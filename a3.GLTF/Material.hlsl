#include "Object.h"
#include "Light.h"
#include "ShaderCommon.h"
#include "PBRMaterial.h"

Texture2D DiffuseTexture : register(t7);
Texture2D MetalRoughTexture : register(t8);
#ifdef NORMAL_MAP
Texture2D NormalMapTexture : register(t9);
#endif // NORMAL_MAP

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 worldPos: POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
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

VSOut VS(float3 pos : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD
#ifdef NORMAL_MAP
    , float4 tangent: TANGENT
#endif //  NORMAL_MAP
#ifdef SKINNED
    , uint4 joints : TEXCOORD1
    , float4 weights : TEXCOORD2
#endif
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

    float4x4 _transform = nodeTransform[jointIdx.x] * weights.x
        + nodeTransform[jointIdx.y] * weights.y
        + nodeTransform[jointIdx.z] * weights.z
        + nodeTransform[jointIdx.w] * weights.w;
    float4x4 _transformNormals = nodeNormalTransform[jointIdx.x] * weights.x
        + nodeNormalTransform[jointIdx.y] * weights.y
        + nodeNormalTransform[jointIdx.z] * weights.z
        + nodeNormalTransform[jointIdx.w] * weights.w;
#else
    float4x4 _transform = nodeTransform[nodeIndex.x];
    float4x4 _transformNormals = nodeNormalTransform[nodeIndex.x];
#endif // !SKINNED

    float4 worldPos = mul(_transform, float4(pos, 1.0));
    output.worldPos = worldPos.xyz;
    output.pos = mul(VP, worldPos);
    output.normal = mul(_transformNormals, float4(normal, 1.0)).xyz;
    output.uv = uv;
#ifdef NORMAL_MAP
    output.tangent.xyz = mul(_transformNormals, float4(tangent.xyz, 1.0)).xyz;
    output.tangent.w = tangent.w;
#endif //  NORMAL_MAP

    return output;
}

struct PSOut
{
    float4 color : SV_TARGET0;
    float4 emissive : SV_TARGET1;
};

PSOut PS(VSOut input)
{
    float3 normal = normalize(input.normal);
#ifdef NORMAL_MAP
    float3 texNormal = (NormalMapTexture.Sample(MinMagMipLinear, input.uv).xyz - 0.5) * 2.0;
    float3 tangent = normalize(input.tangent.xyz);
    float3 binormal = normalize(cross(normal, tangent));
    binormal = -(binormal * sign(input.tangent.w)); // Mirror binormal as we already mirrored source vectors for cross product
    normal = normalize(texNormal.x * tangent + texNormal.y * binormal + texNormal.z * normal);
#endif // NORMAL_MAP


    float3 n = normal;
    float3 v = normalize(cameraPos.xyz - input.worldPos);

#ifdef PLAIN_METAL_ROUGH
    float roughness = max(pbr.x, 0.001);
    float metalness = pbr.y;
    float occlusion = pbr2.x;
#else
    float3 mr = MetalRoughTexture.Sample(MinMagMipLinear, input.uv).rgb;
    float roughness = max(pbr.x * mr.g, 0.001);
    float metalness = pbr.y * mr.b;
    float occlusion = pbr2.x * mr.r;
#endif

    float dielectricF0 = pbr.z;
#ifdef PLAIN_COLOR
    float4 F0 = metalF0;
#else
    float4 diffuse  = DiffuseTexture.Sample(MinMagMipLinear, input.uv);
    float4 F0 = diffuse * metalF0;
#endif

    if (F0.a < pbr.w)
    {
        discard;
    }

    float3 pbrColor = CalcPBRColor(input.worldPos, n, v, roughness, metalness, dielectricF0, F0.xyz, occlusion);

    //return float4(normal / 2 + 0.5, 1);

    //return float4(input.uv, 0, 1);
    
    PSOut psOut;
    psOut.color = float4(pbrColor, F0.a);
#ifdef EMISSIVE
    psOut.emissive = float4(pbrColor + emissiveFactor.rgb, 0.0);
#else
    psOut.emissive = float4(0,0,0,0);
#endif

    return psOut;
}