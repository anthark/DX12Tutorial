#ifndef _DIRECTIONS_H
#define _DIRECTIONS_H

struct Dirs
{
    float3 n;
    float3 v;
};

Dirs CalcDirs(in float3 inNormal, in float2 uv, in float4 inTangent, in float3 worldPos)
{
    float3 normal = normalize(inNormal);
#ifdef NORMAL_MAP
    float3 texNormal = (NormalMapTexture.Sample(MinMagMipLinear, uv).xyz - 0.5) * 2.0;
    float3 tangent = normalize(inTangent.xyz);
    float3 binormal = normalize(cross(normal, tangent));
    binormal = -(binormal * sign(inTangent.w)); // Mirror binormal as we already mirrored source vectors for cross product
    normal = normalize(texNormal.x * tangent + texNormal.y * binormal + texNormal.z * normal);
#endif // NORMAL_MAP

    Dirs d;
    d.n = normal;
    d.v = normalize(cameraPos.xyz - worldPos);

    return d;
}

#endif // _DIRECTIONS_H