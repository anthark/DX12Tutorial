#ifndef _PARTICLE_H
#define _PARTICLE_H

#include "ShaderCommon.h"

#ifndef __cplusplus
#include <GLTFObjectData.h>
#include "ParticleData.h"
#endif // __cplusplus

CONST_BUFFER(SplitData, 2)
GLTF_SPLIT_DATA

CONST_BUFFER(ObjectData, 3)
PARTICLE_DATA

#endif // _PARTICLE_H
