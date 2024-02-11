#include "Object.h"
#include "Light.h"
#include "ShaderCommon.h"

Texture2DArray DiffuseTexture : register(t32);
SamplerState MinMagMipLinear : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut VS(float3 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOut output;

    float4x4 _transform = transform;

    float3 basePos = transform._m03_m13_m23;
    float3 xAxis = inverseView._m00_m10_m20;
    float3 yAxis = float3(0,1,0);

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

PSOut PS(VSOut input)
{
    float val = DiffuseTexture.Sample(MinMagMipLinear, float3(input.uv, ((int)sceneTime) % 128)).r;

    PSOut psOut;
    psOut.color.xyz = float3(0.925, 0.486, 0.127) * val * 10.0;
    psOut.color.w = val;
    psOut.emissive = float4(0,0,0,0);

    return psOut;
}