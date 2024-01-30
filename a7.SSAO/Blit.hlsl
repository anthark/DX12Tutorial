Texture2D SrcTexture : register(t32);
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

float4 PS(VSOut input) : SV_TARGET
{
    return float4(SrcTexture.Sample(NoMipSampler, input.uv).xyz, 1.0);
}
