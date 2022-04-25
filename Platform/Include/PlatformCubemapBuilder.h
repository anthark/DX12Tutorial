#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"

namespace Platform
{

class PLATFORM_API CubemapBuilder
{
public:
    struct InitParams
    {
        int cubemapRes;
        int cubemapMips;
        int irradianceRes;
        int envRes;
        int roughnessMips;
    };

    struct InitLocalParams
    {
        Point2f pos = Point2f();
        float size = 0.0f;
        Point2i grid = Point2i();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = D3D12_CPU_DESCRIPTOR_HANDLE();

        int irradianceRes;
        int envRes;
        int roughnessMips;
    };

    struct Cubemap
    {
        Platform::GPUResource cubemap;
        Platform::GPUResource irradianceMap;
        Platform::GPUResource envMap;
    };

public:
    CubemapBuilder();
    virtual ~CubemapBuilder();

    bool Init(BaseRenderer* pRenderer, const std::vector<std::tstring>& hdriFiles, const InitParams& params);
    bool InitLocal(const InitLocalParams& localParams);
    void Term();

    inline bool HasCubemapsToBuild() const { return !m_hdriFiles.empty(); }
    inline std::tstring GetCurrentCubemapName() const { return m_hdriFiles.front(); }
    bool RenderCubemap();
    inline const std::vector<std::string>& GetLoadedCubemaps() const { return m_loadedCubemaps; }
    inline const Cubemap& GetCubemap(size_t idx) const { return m_cubemaps[idx]; }
    inline size_t GetCubemapCount() const { return m_cubemaps.size(); }

    inline bool HasLocalCubemapsToBuild() const {
        return m_builtLocalCubemaps < m_localCubemapParams.grid.x * m_localCubemapParams.grid.y;
    }
    inline size_t GetBuiltLocalCubemaps() const { return m_builtLocalCubemaps; }
    inline size_t GetLocalCubemapsToBuild() const {
        return (size_t)m_localCubemapParams.grid.x * m_localCubemapParams.grid.y;
    }
    bool RenderLocalCubemap();
    inline const InitLocalParams& GetLocalParams() const { return m_localCubemapParams; }
    inline const GPUResource GetLocalCubemapIrradiance() const { return m_localCubemapIrradianceArray; }
    inline const GPUResource GetLocalCubemapEnvironment() const { return m_localCubemapEnvironmentArray; }

private:
    bool CreateCubeMapRT(int rtRes);
    void DestroyCubeMapRT();
    bool LoadHDRTexture(LPCTSTR filename, Platform::GPUResource* pResource);
    bool BuildCubemapMips(const Platform::GPUResource& resource, bool alpha);

private:
    std::vector<std::tstring> m_hdriFiles;
    BaseRenderer* m_pRenderer;

    D3D12_CPU_DESCRIPTOR_HANDLE m_cubeMapRTV;
    Platform::GPUResource m_cubeMapRT;

    InitParams m_params;

    std::vector<Cubemap> m_cubemaps;

    Platform::GPUResource m_textureForDelete;

    BaseRenderer::GeometryState m_equirectToCubemapFaceState;
    BaseRenderer::GeometryState m_irradianceConvolutionState;
    BaseRenderer::GeometryState m_irradianceConvolutionStateAlphaCutoff;
    BaseRenderer::GeometryState m_environmentConvolutionState;
    BaseRenderer::GeometryState m_environmentConvolutionStateAlphaCutoff;
    BaseRenderer::GeometryState m_simpleCopyState;
    BaseRenderer::GeometryState m_simpleCopyStateAlpha;

    std::vector<std::string> m_loadedCubemaps;

    // Local cubemaps related
    InitLocalParams m_localCubemapParams;
    size_t m_builtLocalCubemaps;
    Platform::GPUResource m_cubeMapDS;
    Platform::GPUResource m_tempLocalCubemap;
    Platform::GPUResource m_localCubemapIrradianceArray;
    Platform::GPUResource m_localCubemapEnvironmentArray;
};

} // Platform
