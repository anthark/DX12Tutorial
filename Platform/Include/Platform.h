#pragma once

template <typename T>
T Align(const T& value, const T& alignment)
{
    return ((value + alignment - 1) / alignment)*alignment;
}

#define TERM_RELEASE(a) if ((a) != nullptr) {\
    (a)->Term();\
    delete a;\
    a = nullptr;\
}

#define D3D_RELEASE(a) if ((a) != nullptr) {\
    (a)->Release();\
    (a) = nullptr;\
}

#if defined(_DEBUG) || defined(_RELEASE)
#define D3D_CHECK(a) \
if (SUCCEEDED(hr))\
{\
    hr = (a);\
    if (!SUCCEEDED(hr))\
    {\
        _com_error err(hr);\
        OutputDebugString(err.ErrorMessage());\
        OutputDebugString(_T("\n"));\
    }\
    assert(SUCCEEDED(hr));\
}
#else
#define D3D_CHECK(a) \
if (SUCCEEDED(hr))\
{\
    hr = (a); \
}
#endif // _DEBUG || _RELEASE