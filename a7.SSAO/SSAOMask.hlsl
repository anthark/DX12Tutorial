#include "SSAOMask.h"

Texture2D DepthTexture : register(t7);
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

float4 PS(VSOut input) : SV_TARGET
{
    float depth = DepthTexture.Sample(NoMipSampler, input.uv).x;

    float4 ndc = float4(PixelToNDC(input.pos.xy).xy, depth, 1);
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

    return float4((float)occlusion / sampleCount.x, 0.0, 0.0, 1.0);
}
