cbuffer Transform : register (b0)
{
    float4x4 VP;
}

cbuffer Object : register (b2)
{
    float4x4 transform;
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VSOut VS(float3 pos : POSITION, float3 color : COLOR)
{
    VSOut output;

    output.pos = mul(VP, mul(transform, float4(pos, 1.0)));
    output.color = float4(color, 1.0);

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
    return input.color;
}