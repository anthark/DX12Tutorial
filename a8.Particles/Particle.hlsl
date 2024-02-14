#include "Particle.h"
#include "Light.h"
#include "ShaderCommon.h"

Texture2DArray DiffuseTexture : register(t32);
Texture1D PaletteTexture : register(t33);

SamplerState MinMagMipLinear : register(s0);
SamplerState MinMagLinearMipPointBorder : register(s2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOut output;

    float4x4 _transform = transform;

    float3 basePos = particleWorldPos.xyz;
    float3 xAxis = float3(1,0,0);
    float3 yAxis = float3(0,1,0);
    if (billboardType == PARTICLE_BILLBOARD_TYPE_VERT)
    {
        xAxis = inverseView._m00_m10_m20;
    }
    else if (billboardType == PARTICLE_BILLBOARD_TYPE_FULL)
    {
        xAxis = inverseView._m00_m10_m20;
        yAxis = inverseView._m01_m11_m21;
    }

    float4 worldPos = float4(
        basePos + pos.x * xAxis + pos.y * yAxis,
        1.0
    );
    output.pos = mul(VP, worldPos);
    output.uv = uv;

    return output;
}

struct PSOut
{
    float4 color : SV_TARGET0;
    float4 emissive : SV_TARGET1;
};

float4 SampleDiffuse(in float2 uv)
{
    float ratio = frac(curFrame);
    int idx = (int)(curFrame - ratio);

    return DiffuseTexture.Sample(MinMagMipLinear, float3(uv, idx)) * (1.0 - ratio)
        + DiffuseTexture.Sample(MinMagMipLinear, float3(uv, (idx + 1) % frameCount)) * ratio;
}

PSOut PS(VSOut input)
{
    float4 color = SampleDiffuse(input.uv);
    if ((particleFlags & PARTICLE_FLAG_USE_PALETTE) != 0)
    {
        color.w = color.r;
        float val = color.r;
        color.xyz = PaletteTexture.Sample(MinMagLinearMipPointBorder, val).xyz;
    }
    else
    {
        if ((particleFlags & PARTICLE_FLAG_HAS_ALPHA) == 0)
        {
            float lum = 0.2126*color.r + 0.7152*color.g + 0.0722*color.b;
            color.w = lum;
        }
    }

    PSOut psOut;
    psOut.color.xyz = color.xyz * particleTint.xyz;
    psOut.color.w = color.w * particleTint.w;
    psOut.emissive = float4(0,0,0,0);

    return psOut;
}