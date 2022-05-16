cbuffer Transform : register (b0)
{
    float4x4 VP;
}

cbuffer Object : register (b1)
{
    float4x4 transform;
}

Texture2D ColorTexture : register(t0);
SamplerState Sampler : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOut output;

    output.pos = mul(VP, mul(transform, float4(pos, 1.0)));
    output.uv = uv;

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    return ColorTexture.Sample(Sampler, input.uv);
}