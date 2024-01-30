cbuffer Transform : register (b0)
{
    float4x4 VP;
}

cbuffer Object : register (b1)
{
    float4x4 transform;
}

Texture2D ColorTexture : register(t32);
SamplerState Sampler : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD;
};

VSOut VS(float3 pos : POSITION, float3 color : COLOR, float2 uv : TEXCOORD)
{
    VSOut output;

    output.pos = float4(pos, 1.0);
    output.uv = uv;
    output.color = float4(color, 1.0);

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    return ColorTexture.Sample(Sampler, input.uv).aaaa * float4(input.color);
}