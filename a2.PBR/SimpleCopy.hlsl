#include "ShaderCommon.h"

Texture2D Source : register(t32);
SamplerState MinMagLinearMipPoint : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 value: TEXCOORD;
};

VSOut VS(float3 pos : POSITION)
{
    VSOut output;

    output.pos = float4(pos, 1.0);
    output.value = float2(pos.x * 0.5 + 0.5, 1.0 - (pos.y * 0.5 + 0.5));

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    return float4(Source.Sample(MinMagLinearMipPoint, input.value.xy).rgb, 1);
}