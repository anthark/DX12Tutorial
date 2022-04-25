#include "stdafx.h"
#include "PlatformUtil.h"

// NearestPowerOf2
UINT NearestPowerOf2(UINT val)
{
    DWORD idx = 0;
    _BitScanReverse(&idx, val);
    UINT res = 1 << idx;

    return (val & ~res) == 0 ? res : (res << 1);
};

// Strip file extension
std::tstring StripExtension(const std::tstring& filename)
{
    size_t dotPos = filename.rfind(_T('.'));
    return filename.substr(0, dotPos);
}

// Short filename (no path)
std::tstring ShortFilename(const std::tstring& filename)
{
    size_t slashPos = filename.rfind(_T('/'));
    if (slashPos == std::string::npos)
    {
        slashPos = filename.rfind(_T('\\'));
    }
    if (slashPos == std::string::npos)
    {
        return filename;
    }
    else
    {
        return filename.substr(slashPos + 1);
    }
}

// Parent folder name
std::tstring GetParentName(const std::tstring& filename)
{
    size_t slashPos = filename.rfind(_T('/'));
    if (slashPos == std::string::npos)
    {
        slashPos = filename.rfind(_T('\\'));
    }
    if (slashPos == std::string::npos)
    {
        return _T("");
    }

    std::tstring folderName = filename.substr(0, slashPos);

    slashPos = folderName.rfind(_T('/'));
    if (slashPos == std::string::npos)
    {
        slashPos = folderName.rfind(_T('\\'));
    }
    if (slashPos == std::string::npos)
    {
        return folderName;
    }
    else
    {
        return folderName.substr(slashPos + 1);
    }
}

// Convert default std::tstring to std::string
std::string ToString(const std::tstring& src)
{
#ifdef _UNICODE
    std::vector<char> buffer((src.length() + 1) * 4);

    size_t converted = 0;
    errno_t res = wcstombs_s(&converted, buffer.data(), (src.length() + 1) * 4, src.c_str(), src.length());
    assert(res == 0 && converted == src.length() + 1);

    return buffer.data();
#else
    return sec;
#endif
}
