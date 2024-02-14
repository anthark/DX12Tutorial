#ifndef _PARTICLE_DATA_H
#define _PARTICLE_DATA_H

#define PARTICLE_FLAG_USE_PALETTE 1
#define PARTICLE_FLAG_HAS_ALPHA 2

#define PARTICLE_BILLBOARD_TYPE_NONE 0
#define PARTICLE_BILLBOARD_TYPE_VERT 1
#define PARTICLE_BILLBOARD_TYPE_FULL 2

#define PARTICLE_DATA \
{\
    float4 particleWorldPos;\
\
    float curFrame;\
    int billboardType;\
    int frameCount;\
    int particleFlags;\
\
    float4 particleTint;\
};

#endif // _PARTICLE_DATA_H
