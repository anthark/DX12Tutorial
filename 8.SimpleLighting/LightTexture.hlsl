#include "Light.h"

CONST_BUFFER(Objects, 2)
{
    float4x4 transform;
    float4x4 transformNormals;
}

Texture2D ColorTexture : register(t7);
SamplerState Sampler : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 worldPos: POSITION1;
    float2 uv : TEXCOORD;
    float3 norm : NORMAL;
};

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD, float3 norm : NORMAL)
{
    VSOut output;

    output.pos = mul(VP, mul(transform, float4(pos, 1.0)));
    output.worldPos = mul(transform, float4(pos, 1.0)).xyz;
    output.uv = uv;
    output.norm = mul(transformNormals, float4(norm, 1.0)).xyz;

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    float4 color = ColorTexture.Sample(Sampler, input.uv);

    float3 light = CalculateLight(input.worldPos, input.norm);

    return float4(color.xyz * light, color.a);
}