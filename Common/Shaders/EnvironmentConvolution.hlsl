#include "EquirectToCubemap.h"

TextureCube ColorTexture : register(t7);
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

float RadicalInverse_VdC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i)/float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 norm, float roughness)
{
    float a = roughness*roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

    // from spherical coordinates to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // from tangent-space vector to world-space sample vector
    float3 up        = abs(norm.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent   = normalize(cross(up, norm));
    float3 bitangent = cross(norm, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + norm * H.z;
    return normalize(sampleVec);
}

float3 CalcConvoluted(in float3 normal)
{
    float3 irradiance = float3(0, 0, 0);

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

            float3 cubemapColor = ColorTexture.Sample(Sampler, sampleVec).rgb;

            irradiance += cubemapColor * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    return irradiance;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float4 PS(VSOut input) : SV_TARGET
{
    float3 norm = normalize(input.worldPos);
    float3 view = norm;
    
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0, 0, 0);
	float prefilteredAlpha = 0;

    float nrSamples = 0;
    static const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, norm, roughness);
        float3 L  = normalize(2.0 * dot(view, H) * H - view);

        float ndotl = max(dot(norm, L), 0.0);
        float ndoth = max(dot(norm, H), 0.0);
        float hdotv = max(dot(H, view), 0.0);

        float D   = DistributionGGX(norm, H, roughness);
        float pdf = (D * ndoth / (4.0 * hdotv)) + 0.0001; 

        float resolution = 512.0; // resolution of source cubemap (per face)
        float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
        float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

        float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

        if(ndotl > 0.0)
        {
			float4 color = ColorTexture.SampleLevel(Sampler, L, mipLevel);
			{
				prefilteredColor += color.rgb * ndotl;
				prefilteredAlpha += color.a;
				totalWeight      += ndotl;
                ++nrSamples;
			}
        }
    }

    prefilteredColor = totalWeight > 0 ? prefilteredColor / totalWeight : float3(0,0,0);
	prefilteredAlpha = nrSamples > 0 ? prefilteredAlpha / nrSamples : 0;

	float4 color = float4(prefilteredColor, prefilteredAlpha);

    return color;
}