#ifndef _PARTICLE_DATA_H
#define _PARTICLE_DATA_H

#define PARTICLE_FLAG_USE_PALETTE 1
#define PARTICLE_FLAG_HAS_ALPHA 2

#define PARTICLE_DATA \
{\
    float4 particleWorldPos;\
\
    float2 curFrame;\
    int frameCount;\
    int particleFlags;\
};

#endif // _PARTICLE_DATA_H
