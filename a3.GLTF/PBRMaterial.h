#ifndef _PBRMATERIAL_H
#define _PBRMATERIAL_H

#include "ShaderCommon.h"

#ifndef __cplusplus

TextureCube DiffuseIrradianceMap : register(t0);
Texture2D BRDFLut : register(t1);
TextureCube EnvMap : register(t2);

SamplerState MinMagMipLinear : register(s0);
SamplerState MinMagLinearMipPoint : register(s1);
SamplerState MinMagLinearMipPointBorder : register(s2);

// Trowbridge-Reitz GGX
float NormalDistributionFunction(in float3 n, in float3 v, in float3 l, in float roughness)
{
    float3 halfVec = normalize(l + v);
    float ndoth = max(dot(halfVec, n), 0.0);
    float denom = (ndoth*ndoth*(roughness * roughness-1)+1);
    float ndf = roughness / denom;
    ndf *= ndf;
    ndf /= PI;
    return ndf;
}

// Schlick-GGX
float GeometryFunctionSub(in float3 n, in float3 r, float k)
{
    float ndotr = max(dot(n,r),0);
    return ndotr / (ndotr * (1 - k) + k);
}

// Smith's method
float GeometryFunction(in float3 n, in float3 v, in float3 l, float roughness)
{
    float k = (roughness + 1) * (roughness + 1) / 8;
    return GeometryFunctionSub(n, v, k) * GeometryFunctionSub(n, l, k);
}

// Fresnel-Schlick
float3 FresnelFunction(in float dF0, in float3 mF0, in float metalness, in float3 v, in float3 l)
{
    float3 F0 = lerp(float3(dF0, dF0, dF0), mF0, metalness);

    float3 halfVec = normalize(l + v);
    float hdotv = max(dot(halfVec, v), 0.0);

    return F0 + (float3(1.0,1.0,1.0) - F0) * pow(1.0 - hdotv, 5.0);
}

float3 FresnelSchlickRoughnessFunction(float3 F0, float roughness, in float3 v, in float3 n)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(max(1.0 - max(dot(v, n), 0.0), 0.0), 5.0);
}

float4 CalcPBRColor(in float3 worldPos, in float3 n, in float3 v, in float roughness, in float metalness, in float dielectricF0, in float3 metalF0, in float occlusion)
{
    float4 color = float4(0,0,0,1);

    int renderMode = intSceneParams.x;

    float3 r = reflect(-v, n);

    for (unsigned int i = 0; i < lightCount; i++)
    {
        LightValue light = CalculateLight(worldPos, i);
        float3 l = -light.lightDir;

        float3 radiance = light.color * light.attenuation;
        float ndotl = max(dot(n, l), 0.0);

        switch (renderMode)
        {
            case RENDER_MODE_LIGHTING:
                {
                    float NDF = NormalDistributionFunction(n, v, l, roughness);
                    float G = GeometryFunction(n, v, l, roughness);
                    float3 F = FresnelFunction(dielectricF0, metalF0.xyz, metalness, v, l);

                    float3 kS = F;
                    float3 kD = float3(1.0, 1.0, 1.0) - kS;
                    kD *= 1.0 - metalness;

                    float3 numerator    = NDF * G * F;
                    float denominator = 4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0);
                    float3 specular     = numerator / max(denominator, 0.001);  

                    // add to outgoing radiance Lo
                    color.xyz += (kD * metalF0.xyz / PI + specular) * radiance * ndotl;
                }
                break;

            case RENDER_MODE_DIFFUSE:
                {
                    float kD = 1.0 - metalness;

                    color.xyz += (kD * metalF0.xyz / PI) * radiance * ndotl;
                }
                break;

            case RENDER_MODE_SPECULAR:
                {
                    float NDF = NormalDistributionFunction(n, v, l, roughness);
                    float G = GeometryFunction(n, v, l, roughness);
                    float3 F = FresnelFunction(dielectricF0, metalF0.xyz, metalness, v, l);

                    float3 kS = F;
                    float3 kD = float3(1.0, 1.0, 1.0) - kS;
                    kD *= 1.0 - metalness;

                    float3 numerator    = NDF * G * F;
                    float denominator = 4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0);
                    float3 specular     = numerator / max(denominator, 0.001);  

                    color.xyz += specular;
                }
                break;

            case RENDER_MODE_SPEC_NORMAL_DISTRIBUTION:
                {
                    float NDF = NormalDistributionFunction(n, v, l, roughness);
                    color.xyz += float3(NDF, NDF, NDF);
                }
                break;

            case RENDER_MODE_SPEC_GEOMETRY:
                {
                    float G = GeometryFunction(n, v, l, roughness);
                    color.xyz += float3(G, G, G);
                }
                break;

            case RENDER_MODE_SPEC_FRESNEL:
                {
                    float3 F = dot(n, l) < 0 ? float3(0,0,0) : FresnelFunction(dielectricF0, metalF0.xyz, metalness, v, l);
                    color.xyz += F;
                }
                break;
        }
    }

    static const float MAX_REFLECTION_LOD = 4.0;

    switch (renderMode)
    {
        case RENDER_MODE_IBL_DIFFUSE:
            {
                float3 irradiance = DiffuseIrradianceMap.Sample(MinMagMipLinear, n).rgb;
                float3 diffuse    = irradiance * metalF0.xyz;
                color.xyz += diffuse;
            }
            break;
    
        case RENDER_MODE_IBL_SPEC_ENV:
            {
                float3 prefilteredColor = EnvMap.SampleLevel(MinMagMipLinear, r, roughness * MAX_REFLECTION_LOD).rgb;
                color.xyz += prefilteredColor;
            }
            break;

        case RENDER_MODE_IBL_SPEC_FRESNEL:
            {
                float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), metalF0.xyz, metalness);
                float3 F = FresnelSchlickRoughnessFunction(F0, roughness, v, n);
                color.xyz += F;
            }
            break;

        case RENDER_MODE_IBL_SPEC_BRDF:
            {
                float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), metalF0.xyz, metalness);
                float3 F = FresnelSchlickRoughnessFunction(F0, roughness, v, n);
                float ndotv = dot(n,v);
                float2 envBRDF  = BRDFLut.Sample(MinMagLinearMipPointBorder, float2(max(ndotv, 0.0), roughness)).rg;
                float3 specular = F * envBRDF.x + envBRDF.y;

                color.xyz += specular;
            }
            break;

        case RENDER_MODE_NORMAL:
            {
                color.xyz = (n + float3(1,1,1)) * 0.5;
            }
            break;
    }

    // Final composition
    if (renderMode == RENDER_MODE_LIGHTING)
    {
        static const float MAX_REFLECTION_LOD = 4.0;
        float3 prefilteredColor = EnvMap.SampleLevel(MinMagMipLinear, r, roughness * MAX_REFLECTION_LOD).rgb;

        float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), metalF0.xyz, metalness);
        float3 F = FresnelSchlickRoughnessFunction(F0, roughness, v, n);
        float2 envBRDF  = BRDFLut.Sample(MinMagLinearMipPointBorder, float2(max(dot(n, v), 0.0), roughness)).rg;

        float3 specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metalness;
        float3 irradiance = DiffuseIrradianceMap.Sample(MinMagMipLinear, n).rgb;
        float3 diffuse    = irradiance * metalF0.xyz;
        float3 ambient    = (kD * diffuse + specular);

        color.xyz += ambient;
        color.xyz *= occlusion;
    }

    return color;
}

#endif // __cplusplus

#endif // _PBRMATERIAL_H
