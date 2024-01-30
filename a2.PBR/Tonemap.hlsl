#include "Tonemap.h"

Texture2D HDRTexture : register(t32);
SamplerState NoMipSampler : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut VS(float2 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOut output;

    output.pos = float4(pos, 0.5, 1);
    output.uv = uv;

    return output;
}

static const float A = 0.1; // Shoulder Strength
static const float B = 0.50; // Linear Strength
static const float C = 0.1; // Linear Angle
static const float D = 0.20; // Toe Strength
static const float E = 0.02; // Toe Numerator
static const float F = 0.30; // Toe Denominator
                             // Note: E/F = Toe Angle
static const float W = 11.2; // Linear White Point Value

float3 Uncharted2Tonemap(float3 x)
{
   return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 TonemapFilmic(float3 color, float E)
{
    float exposure = sceneParams.x;
    float3 curr = Uncharted2Tonemap(2.0 * exposure * E * color);
    float3 whiteScale = 1.0f / Uncharted2Tonemap(W);
    return curr * whiteScale;
}

float4 PS(VSOut input) : SV_TARGET
{
    float4 color = HDRTexture.Sample(NoMipSampler, input.uv);

    int renderMode = intSceneParams.x;

    switch (renderMode)
    {
        case RENDER_MODE_LIGHTING:
            return float4(pow(TonemapFilmic(color.xyz, exposureAdapted), 1.0/2.2), 1); // Uncharted2 with gamma correction and eye adaptation
            break;

        case RENDER_MODE_SPECULAR:
        case RENDER_MODE_DIFFUSE:
        case RENDER_MODE_IBL_DIFFUSE:
        case RENDER_MODE_IBL_SPEC_ENV:
        case RENDER_MODE_IBL_SPEC_FRESNEL:
        case RENDER_MODE_IBL_SPEC_BRDF:
            color.xyz = color.xyz / maxValue; // Normalized value
            return float4(color.xyz, 1.0);
            break;

        case RENDER_MODE_SPEC_NORMAL_DISTRIBUTION:
        case RENDER_MODE_SPEC_GEOMETRY:
        case RENDER_MODE_SPEC_FRESNEL:
            return float4(color.xyz, 1.0);
            break;
    }

    return float4(1.0, color.yz, 1.0); // To show error
}