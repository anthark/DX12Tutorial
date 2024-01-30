#include "EquirectToCubemap.h"

Texture2D ColorTexture : register(t32);
SamplerState Sampler : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 worldPos: POSITION1;
};

VSOut VS(float3 pos : POSITION)
{
    VSOut output;

    output.pos = mul(FaceVP, float4(pos, 1.0));
    output.worldPos = mul(transform, float4(pos,1.0)).xyz;

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    float2 uv = SampleSphericalMap(normalize(input.worldPos.xyz));
    float3 texColor = ColorTexture.Sample(Sampler, uv).xyz;

    float4 color = float4(texColor.xyz, 1);

    return color;
}