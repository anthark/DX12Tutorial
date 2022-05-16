#include "Object.h"

TextureCube CubemapTexture : register(t3);
SamplerState Sampler : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 norm : NORMAL;
};

VSOut VS(float3 pos : POSITION, float3 norm : NORMAL)
{
    VSOut output;
    
    output.pos = float4(mul(VP, mul(transform, float4(pos, 1.0))).xy, 1, 1);
    output.norm = mul(transformNormals, float4(norm, 1.0)).xyz;

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    float4 color = CubemapTexture.Sample(Sampler, normalize(input.norm));

    return float4(color.xyz, 1);
}