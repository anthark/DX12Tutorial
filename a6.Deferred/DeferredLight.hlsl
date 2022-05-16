#include "ShaderCommon.h"
#include "PBRMaterial.h"

#include "LightGeometry.h"

Texture2D Albedo : register (t7);
Texture2D TexF0 : register (t8);
Texture2D N : register (t9);
Texture2D Emissive : register (t10);
Texture2D DepthTexture : register (t11);

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut VS(float3 pos : POSITION)
{
    VSOut output;
#ifdef WORLD_POS
    output.pos = mul(VP, mul(lightTransform, float4(pos, 1)));
#else
    output.pos = float4(pos, 1);
#endif // !WORLD_POS
    return output;
}

struct PSOut
{
    float4 color : SV_Target0;
#ifdef AMBIENT_COLOR
    float4 em : SV_Target1;
#endif // AMBIENT_COLOR
};

PSOut PS(in VSOut input)
{
    int lightIdx = lightIndex.x;

    float2 uv = input.pos.xy / imageSize.xy;
    
    // Calculate world pos
    float4 ndc = float4(uv.x * 2.0 - 1.0, ((1.0 - uv.y) * 2.0 - 1.0), DepthTexture.Sample(MinMagMipLinear, uv).x, 1.0);
    float4 homoViewPos = mul(inverseProj, ndc);
    homoViewPos.xyzw /= homoViewPos.w;
    float4 worldPos = mul(inverseView, homoViewPos);

#ifdef TEST
    // Some comment here to test shader cache
    {
        float distSqr = (worldPos.x - lights[lightIdx].pos.x)*(worldPos.x - lights[lightIdx].pos.x)
                + (worldPos.y - lights[lightIdx].pos.y)*(worldPos.y - lights[lightIdx].pos.y)
                + (worldPos.z - lights[lightIdx].pos.z)*(worldPos.z - lights[lightIdx].pos.z);
        //float attenuation = 1.0 - smoothstep(lights[lightIdx].cutoffDist.x, 1.21*lights[lightIdx].cutoffDist.x, distSqr);

        PSOut res;
        res.color = float4(lights[lightIdx].color.xyz / 20.0/* * attenuation*/, 0);
        return res;
    }
#endif // TEST

    
    // Calculate view vector
    float3 v = normalize(cameraPos.xyz - worldPos.xyz);

    // Get normal and PBR type
    float4 normal = N.Sample(MinMagMipLinear, uv);
    bool metallicRoughness = normal.w < 0.5;
    
    normal.xyz = normal.xyz * 2.0 - 1.0;
    
    float4 pbrColor;

    // PBR type branch
    if (metallicRoughness)
    {
        float3 metalF0 = Albedo.Sample(MinMagMipLinear, uv).xyz;
        float roughness = Albedo.Sample(MinMagMipLinear, uv).w;
        float dielectricF0 = TexF0.Sample(MinMagMipLinear, uv).x;
        float metalness = TexF0.Sample(MinMagMipLinear, uv).w;
    
        float3 r = reflect(-v, normal.xyz);
    
        pbrColor.xyz = CalcPBRLight(lightIdx, worldPos.xyz, normal.xyz, v, roughness, dielectricF0, metalF0, metalness );
#ifdef AMBIENT_COLOR
        pbrColor.xyz += CalcPBRAmbient(worldPos.xyz, r, roughness, dielectricF0, metalF0, metalness, normal.xyz, v);
#endif // AMBIENT_COLOR
    }
    else
    {
        float3 Cdiff = Albedo.Sample(MinMagMipLinear, uv).xyz;
        float roughness = Albedo.Sample(MinMagMipLinear, uv).w;
        float3 F0 = TexF0.Sample(MinMagMipLinear, uv).xyz;

        float3 r = reflect(-v, normal.xyz);
        pbrColor.xyz = CalcPBRLight_KHRSpecGloss(lightIdx, worldPos.xyz, normal.xyz, v, roughness, F0, Cdiff);
#ifdef AMBIENT_COLOR
        pbrColor.xyz += CalcPBRAmbient_KHRSpecGloss(worldPos.xyz, r, roughness, F0, normal.xyz, v, Cdiff);
#endif // AMBIENT_COLOR
    }

    float4 emissive = Emissive.Sample(MinMagMipLinear, uv);
    
    PSOut res;
    res.color = pbrColor;
#ifdef AMBIENT_COLOR
    res.em = float4(pbrColor.xyz * emissive.w + emissive.xyz, 0);
#endif // AMBIENT_COLOR

    return res;	
}
