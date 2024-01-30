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
   float3 curr = Uncharted2Tonemap(E * color);
   float3 whiteScale = 1.0f / Uncharted2Tonemap(W);
   return curr * whiteScale;
}

float4 PS(VSOut input) : SV_TARGET
{
    float4 color = HDRTexture.Sample(NoMipSampler, input.uv);

    switch (tonemapMode.x)
    {
        case TONEMAP_MODE_NONE:
            break;

        case TONEMAP_MODE_NORMALIZE:
            color.xyz = color.xyz / maxValue; // Normalized value
            break;

        case TONEMAP_MODE_REINHARD_SIMPLE:
            color.xyz = color.xyz/(color.xyz + 1.0);  // Reinhard simple
            break;

        case TONEMAP_MODE_UNCHARTED2:
            return float4(TonemapFilmic(color.xyz, exposure), 1); // Uncharted2
            break;

        case TONEMAP_MODE_UNCHARTED2_SRGB:
            return float4(pow(TonemapFilmic(color.xyz, exposure), 1.0/2.2), 1); // Uncharted2 with gamma correction
            break;

        case TONEMAP_MODE_UNCHARTED2_SRGB_EYE_ADAPTATION:
            return float4(pow(TonemapFilmic(color.xyz, exposureAdapted), 1.0/2.2), 1); // Uncharted2 with gamma correction and eye adaptation
            break;
    }

    return float4(color.xyz, 1.0);
}