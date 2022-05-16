#ifndef _OBJECT_H
#define _OBJECT_H

#include "ShaderCommon.h"

#ifndef __cplusplus
#include <GLTFObjectData.h>
#endif // __cplusplus

CONST_BUFFER(SplitData, 2)
GLTF_SPLIT_DATA

CONST_BUFFER(ObjectData, 3)
GLTF_OBJECT_DATA

#endif // _OBJECT_H
