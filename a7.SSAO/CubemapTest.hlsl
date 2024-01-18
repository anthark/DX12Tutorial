#include "CubemapTestGeom.h"

TextureCubeArray LocalIrradianceMapArray : register(t5);
TextureCubeArray LocalEnvironmentMapArray : register(t6);

SamplerState MinMagMipLinear : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 norm : NORMAL;
	float3 worldPos : POSITION;
};

VSOut VS(float3 pos : POSITION, float3 norm : NORMAL)
{
    VSOut output;
    
	float4 worldPos = mul(transform, float4(pos, 1.0));
    output.pos = mul(VP, worldPos);
    output.norm = norm;
	output.worldPos = worldPos.xyz;

    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
	float3 normal = normalize(input.worldPos - cubeParams.xyz);
	
	float4 color = LocalEnvironmentMapArray.Sample(MinMagMipLinear, float4(normal, cubeParams.w));

    return color;
}