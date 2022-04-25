#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#define NOMINMAX
#include <windows.h>

#include <tchar.h>
#include <assert.h>
#include <comdef.h>

#include <dxgi.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3dx12.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "PlatformApi.h"