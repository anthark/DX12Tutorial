#include "EquirectToCubemap.h"

TextureCube ColorTexture : register(t32);
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

float4 CalcConvoluted(in float3 normal)
{
    float3 irradiance = float3(0, 0, 0);
	float alpha = 0;

    float3 up    = float3(0.0, 1.0, 0.0);
    float3 right = cross(up, normal);
    up         = cross(normal, right);

    float sampleDelta = 0.00625;
    float nrSamples = 0.0; 
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; 

            float4 cubemapColor = ColorTexture.Sample(Sampler, sampleVec);
			{
				irradiance += cubemapColor.rgb * cos(theta) * sin(theta);
				alpha += cubemapColor.a;
				nrSamples++;
			}
        }
    }
    return nrSamples > 0 ? float4(PI * irradiance / nrSamples, alpha / nrSamples) : float4(0,0,0,0);
}

float4 CalcConvoluted_Mine(in float3 normal)
{
    float3 up    = float3(0.0, 1.0, 0.0);
    float3 right = cross(up, normal);
    up         = cross(normal, right);

    static const int N1 = 200;
    static const int N2 = 50;

	float nrSamples = 0.0; 
    float3 irradiance = float3(0, 0, 0);
	float alpha = 0;
    for (int i = 0; i < N1; i++)
    {
        for (int j = 0; j < N2; j++)
        {
            float phi = i * (2 * PI / N1);
            float theta = j * (PI / 2 / N2);
            // spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // ... и из касательного пространства в мировое
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
			float4 cubemapColor = ColorTexture.Sample(Sampler, sampleVec);
			{
				irradiance += cubemapColor.xyz * cos(theta) * sin(theta);
				alpha += cubemapColor.a;
				nrSamples++;
			}
        }
    }
    return nrSamples > 0 ? float4(PI * irradiance / nrSamples, alpha / nrSamples) : float4(0,0,0,0);
}

float4 PS(VSOut input) : SV_TARGET
{
    //return CalcConvoluted(normalize(input.worldPos));

    return CalcConvoluted_Mine(normalize(input.worldPos));
}