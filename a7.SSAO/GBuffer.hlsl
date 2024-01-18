#include "Object.h"

Texture2D DiffuseTexture : register(t7);
#ifdef KHR_SPECGLOSS
Texture2D SpecularGlossinessTexture : register(t8);
#else
Texture2D MetalRoughTexture : register(t8);
#endif
#ifdef NORMAL_MAP
Texture2D NormalMapTexture : register(t9);
#endif // NORMAL_MAP
#ifdef EMISSIVE_MAP
Texture2D EmissiveMapTexture : register(t10);
#endif // EMISSIVE_MAP

SamplerState MinMagMipLinear : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
	float3 worldPos: POSITION1;
	float3 normal : NORMAL;
    float2 uv : TEXCOORD;
#ifdef NORMAL_MAP
    float3 tangent : TANGENT;
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

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD, float3 normal : NORMAL
#ifdef NORMAL_MAP
    , float3 tangent: TANGENT
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
    output.worldPos = worldPos.xyz;
    output.pos = mul(VP, worldPos);
    output.normal = mul(modelNormalTransform, mul(_transformNormals, float4(normal, 1.0))).xyz;
    output.uv = uv;
#ifdef NORMAL_MAP
    output.tangent = tangent;
#endif //  NORMAL_MAP

    return output;
}

struct Dirs
{
float3 n;
float3 v;
};

Dirs CalcDirs(in float3 inNormal, in float2 uv, in float3 inTangent, in float3 worldPos)
{
    float3 normal = normalize(inNormal);
#ifdef NORMAL_MAP
    float3 texNormal = (NormalMapTexture.Sample(MinMagMipLinear, uv).xyz - 0.5) * 2.0;
    float3 tangent = normalize(inTangent);
    float3 binormal = normalize(cross(normal, tangent));
    normal = normalize(texNormal.x * binormal + texNormal.y * tangent + texNormal.z * normal);
#endif // NORMAL_MAP

    Dirs d;
    d.n = normal;
    d.v = normalize(cameraPos.xyz - worldPos);

    return d;
}

struct PSOut
{
    float4 color : SV_TARGET0;
    float4 F0: SV_TARGET1;
    float4 normal : SV_TARGET2;
#ifdef EMISSIVE
    float4 emissive : SV_TARGET3;
#endif // EMISSIVE
};

#ifdef KHR_SPECGLOSS

PSOut PS(VSOut input)
{
#ifdef NORMAL_MAP
    Dirs d = CalcDirs(input.normal, input.uv, input.tangent, input.worldPos);
#else
    Dirs d = CalcDirs(input.normal, input.uv, float3(0,0,0), input.worldPos);
#endif

    #ifdef PLAIN_COLOR
        float4 Cdiff = ksgDiffFactor;
    #else
        float4 Cdiff = DiffuseTexture.Sample(MinMagMipLinear, input.uv) * ksgDiffFactor;
    #endif
	
	#ifdef PLAIN_SPEC_GLOSS
        float roughness = 1.0 - pow(ksgSpecGlossFactor.a, 1.0/2.2);
        float3 F0 = ksgSpecGlossFactor.xyz;
    #else
        float4 sg = SpecularGlossinessTexture.Sample(MinMagMipLinear, input.uv);
        float roughness = (1.0 - sg.a) * ksgSpecGlossFactor.a;
        float3 F0 = sg.rgb * ksgSpecGlossFactor.rgb;
    #endif

    float alpha = Cdiff.a;
    #ifdef EMISSIVE_MAP
        alpha += EmissiveMapTexture.Sample(MinMagMipLinear, input.uv).a;
    #endif
    #ifdef ALPHA_KILL
    if (alpha < pbr.w)
    {
        discard;
    }
    #endif // ALPHA_KILL

    PSOut psOut;
    psOut.color = float4(Cdiff.rgb, roughness);
    psOut.F0 = float4(F0, 0.0f);
    psOut.normal = float4(d.n * 0.5 + float3(0.5, 0.5, 0.5), 1.0);
#ifdef EMISSIVE
    #ifdef EMISSIVE_MAP
        psOut.emissive = float4(emissiveFactor.rgb * EmissiveMapTexture.Sample(MinMagMipLinear, input.uv), EmissiveMapTexture.Sample(MinMagMipLinear, input.uv).x);
    #else
        psOut.emissive = float4(emissiveFactor.rgb, 0.0);
    #endif
#endif // EMISSIVE

    return psOut;
}

#else

PSOut PS(VSOut input)
{
#ifdef NORMAL_MAP
    Dirs d = CalcDirs(input.normal, input.uv, input.tangent, input.worldPos);
#else
    Dirs d = CalcDirs(input.normal, input.uv, float3(0,0,0), input.worldPos);
#endif

    #ifdef PLAIN_COLOR
        float4 F0 = metalF0;
    #else
        float4 diffuse  = DiffuseTexture.Sample(MinMagMipLinear, input.uv);
        float4 F0 = diffuse * metalF0;
    #endif

    #ifdef PLAIN_METAL_ROUGH
        float roughness = max(pbr.x, 0.001);
        float metalness = pbr.y;
    #else
        float4 mr = MetalRoughTexture.Sample(MinMagMipLinear, input.uv);
        float roughness = max(pbr.x * mr.g, 0.001);
        float metalness = pbr.y * mr.b;
    #endif

    float dielectricF0 = pbr.z;

    float alpha = F0.a;
    #ifdef EMISSIVE_MAP
        alpha += EmissiveMapTexture.Sample(MinMagMipLinear, input.uv).a;
    #endif
    #ifdef ALPHA_KILL
    if (alpha < pbr.w)
    {
        discard;
    }
    #endif // ALPHA_KILL

    PSOut psOut;
    psOut.color = float4(F0.xyz, roughness);
    psOut.F0 = float4(dielectricF0, dielectricF0, dielectricF0, metalness);
    psOut.normal = float4(d.n * 0.5 + float3(0.5, 0.5, 0.5), 0.0);
#ifdef EMISSIVE
    #ifdef EMISSIVE_MAP
        psOut.emissive = float4(emissiveFactor.rgb * EmissiveMapTexture.Sample(MinMagMipLinear, input.uv), EmissiveMapTexture.Sample(MinMagMipLinear, input.uv).x);
    #else
        psOut.emissive = float4(emissiveFactor.rgb, 0.0);
    #endif
#endif // EMISSIVE

    return psOut;
}

#endif // !KHR_SPECGLOSS