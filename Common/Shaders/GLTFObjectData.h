#ifndef _GLTF_OBJECT_DATA_H
#define _GLTF_OBJECT_DATA_H

#define GLTF_SPLIT_DATA \
{\
    float4x4 transform;\
    float4x4 transformNormals;\
    float4 pbr;\
    float4 pbr2;\
    float4 metalF0;\
    float4 emissiveFactor;\
    float4 ksgDiffFactor;\
    float4 ksgSpecGlossFactor;\
    int4   flags; /* x - receives shadow */ \
    int4   nodeIndex; /* x - node index */ \
};

#define MAX_NODES 200

#define GLTF_OBJECT_DATA \
{\
    float4x4 modelTransform;\
    float4x4 modelNormalTransform;\
    float4x4 nodeTransforms[MAX_NODES];\
    float4x4 nodeNormalTransforms[MAX_NODES];\
    int4 jointIndices[MAX_NODES];\
};

#endif // _GLTF_OBJECT_DATA_H
