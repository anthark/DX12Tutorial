#pragma once

// Comment this and switch Platform project to non-DLL config in solution configuration to use static lib version
#define PLATFORM_DLL

#ifdef PLATFORM_DLL
#ifdef PLATFORM_EXPORTS
    #define PLATFORM_API __declspec(dllexport)
#else
    #define PLATFORM_API __declspec(dllimport)
#endif
#else
#define PLATFORM_API
#endif

#include <string>

#ifdef _UNICODE
namespace std
{
    using tstring = wstring;
}
#else
namespace std
{
    using tstring = string;
}
#endif // !UNICODE
