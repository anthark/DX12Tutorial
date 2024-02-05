#include "SSAOMask.h"

Texture2D DepthTexture : register(t32);
Texture2D Normals : register(t33);
SamplerState NoMipSampler : register(s1);

float3 PixelToNDC(float2 pixCoord)
{
    float2 coord = pixCoord / imageSize.xy * 2.0 - float2(1.0,1.0);
    return float3(float2(coord.x, -coord.y), 0);
}
float2 NDCToPixel(float3 ndcCoord)
{
    return float2(ndcCoord.x*0.5 + 0.5, -ndcCoord.y*0.5 + 0.5) * imageSize.xy;
}
float2 NDCToUV(float3 ndcCoord)
{
    return float2(ndcCoord.x*0.5 + 0.5, -ndcCoord.y*0.5 + 0.5);
}

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

float BasicOcclusion(float2 uv, float2 pos)
{
    float depth = DepthTexture.Sample(NoMipSampler, uv).x;

    float4 ndc = float4(PixelToNDC(pos).xy, depth, 1);
    float4 homoViewPos = mul(invVP, ndc);
    float3 viewPos = homoViewPos.xyz / homoViewPos.w;

    int i = 0;
	int occlusion = 0;
	for (i = 0; i < sampleCount.x; i++)
	{
		float3 samplePos = viewPos + samples[i].xyz * ssaoParams.x;
		float4 samplePosProj = mul(cameraProj, float4(samplePos, 1.0));
		float3 samplePosNDC = samplePosProj.xyz / samplePosProj.w;
		float2 samplePosUV = NDCToUV(samplePosNDC);
		float sampleDepth = DepthTexture.Sample(NoMipSampler, samplePosUV).x;
		if (sampleDepth > samplePosNDC.z)
		{
			++occlusion;
		}
	}
	
	return (float)occlusion;
}

float HalfSphereOcclusion(float2 uv, float2 pos)
{
	float3 normal = normalize(Normals.Sample(NoMipSampler, uv).xyz * 2.0 - 1.0);
	normal = mul(cameraViewNoTrans, float4(normal,1)).xyz;

	float3 rvec = abs(normal.x - 1.0) > 0.01 ? float3(1,0,0) : float3(0,1,0);
	float3 tangent = normalize(rvec - normal * dot(rvec, normal));
	float3 bitangent = cross(normal, tangent);

	float depth = DepthTexture.Sample(NoMipSampler, uv).x;
	float4 ndc = float4(PixelToNDC(pos).xy, depth, 1);
    float4 homoViewPos = mul(invVP, ndc);
    float3 viewPos = homoViewPos.xyz / homoViewPos.w;

	int i = 0;
	int occlusion = 0;
	for (i = 0; i < sampleCount.x; i++)
	{
		float3 samplePos = viewPos 
			+ (samples[i].x * tangent
			+ samples[i].y * bitangent
			+ samples[i].z * normal) * ssaoParams.x;
		float4 samplePosProj = mul(cameraProj, float4(samplePos, 1.0));
		float3 samplePosNDC = samplePosProj.xyz / samplePosProj.w;
		float2 samplePosUV = NDCToUV(samplePosNDC);
		float sampleDepth = DepthTexture.Sample(NoMipSampler, samplePosUV).x;
		if (sampleDepth > samplePosNDC.z)
		{
			++occlusion;
		}
	}
	
	return (float)occlusion;
}

float4 PS(VSOut input) : SV_TARGET
{
	float occlusion = 0.0;

	switch (ssaoMode)
	{
		case 0: // Basic
			occlusion = BasicOcclusion(input.uv, input.pos.xy);
			break;

		case 1: // Half sphere
			occlusion = HalfSphereOcclusion(input.uv, input.pos.xy);
			break;
	}

    float maskValue = (float)occlusion / sampleCount.x;

    return float4(maskValue, maskValue, maskValue, 1.0);
}
