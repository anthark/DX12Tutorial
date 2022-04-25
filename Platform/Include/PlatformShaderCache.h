#pragma once

#include <map>

#include "PlatformDevice.h"

namespace Platform
{

class PLATFORM_API ShaderCache
{
public:

    ShaderCache();
    virtual ~ShaderCache();

    bool Init(Device* pDevice);
    void Term();

    bool CompileShader(LPCTSTR srcFilename, const std::vector<LPCSTR>& defines, const Device::ShaderStage& stage, ID3DBlob** ppShaderBinary);

    bool SaveCache(LPCTSTR filename);
    bool LoadCache(LPCTSTR filename);

    inline bool IsModified() const { return m_modified; }

private:
    struct ShaderBinaryKey
    {
        std::tstring srcFilename;
        std::vector<std::string> defines;
        Device::ShaderStage stage;

        bool operator<(const ShaderBinaryKey& rhs) const;
    };

    struct ShaderBinary
    {
        std::set<std::tstring> sources;
        ID3DBlob* pBinary;
    };

    using ShaderBinaryMap = std::map<ShaderBinaryKey, ShaderBinary>;

private:
    UINT32 CalcCRC32(const std::tstring& filename);

private:
    ShaderBinaryMap m_shaderMap;

    Device* m_pDevice;

    std::map<std::tstring, UINT32> m_srcFilesCRC32;
    bool m_modified;
};

} // Platform
