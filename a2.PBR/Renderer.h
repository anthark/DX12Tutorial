#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include "Object.h"

struct Lights;

struct SceneParameters
{
    enum SceneGeometryType
    {
        SceneGeometryTypeSingleObject = 0,
        SceneGeometryTypeObjectsGrid
    };

    enum RenderMode
    {
        RenderModeLighting = 0,
        RenderModeDiffuse,
        RenderModeIBLDiffuse,
        RenderModeSpecular,
        RenderModeSpecularNormalDestribution,
        RenderModeSpecularGeometry,
        RenderModeSpecularFresnel,
        RenderModeSpecularIBLEnv,
        RenderModeSpecularIBLFresnel,
        RenderModeSpecularIBLBRDF
    };

    // Scene setup
    float exposure;
    bool showGrid;
    bool showCubemap;
    SceneGeometryType geomType;
    RenderMode renderMode;
    int cubemapIdx;

    // Object data
    float roughness;
    float metalness;
    float dielectricF0;
    float metalF0Srgb[3];
    float metalF0Linear[3];

    bool showMenu;

    SceneParameters();
};

class Renderer : public Platform::BaseRenderer, public Platform::CameraControlEuler
{
public:
    Renderer(Platform::Device* pDevice);
    virtual ~Renderer();

    virtual bool Init(HWND hWnd) override;
    virtual void Term() override;

    virtual bool Update(double elapsedSec, double deltaSec) override;
    virtual bool Render() override;

    virtual bool OnKeyDown(int virtualKeyCode) override;

protected:
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:
    static const Point4f BackColor;
    static const DXGI_FORMAT HDRFormat;
    static const UINT CubemapRes = 512;
    static const UINT CubemapMips = 9;
    static const UINT IrradianceRes = 32;
    static const UINT EnvRes = 128;
    static const UINT BRDFRes = 512;

private:

    struct Geometry : BaseRenderer::Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objData); return &objData; }

        ObjectData objData;
    };

private:
    void MeasureLuminance();
    void Tonemap();
    void SetupLights(Lights* pLights);

    bool CreateHDRTexture();
    void DestroyHDRTexture();

    bool CreateBRDFTexture();
    void DestroyBRDFTexture();

    bool CreateComputePipeline();
    void DestroyComputePipeline();

    bool LoadHDRTexture(LPCTSTR filename, Platform::GPUResource* pResource);

    bool CreateCubeMapRT();
    void DestroyCubeMapRT();

    bool IsCreationFrame() const { return m_firstFrame; }
    bool RenderCubemaps();
    bool HasCubemapsForBuild(std::wstring& name);
    void ClearCubemapIntermediates();

    void IntegrateBRDF();

    bool BuildCubemapMips(const Platform::GPUResource& resource);

private:
    std::vector<Geometry> m_geometries;
    std::vector<Geometry> m_serviceGeometries;

    Platform::GPUResource m_hdrRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hdrSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrSRVCpu;

    Platform::GPUResource m_cubeMapRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cubeMapRTV;

    std::vector<Platform::GPUResource> m_cubeMaps;

    std::vector<Platform::GPUResource> m_irradianceCubemaps;
    std::vector<Platform::GPUResource> m_envCubemaps;

    bool m_brdfReady;
    Platform::GPUResource m_brdf;
    D3D12_CPU_DESCRIPTOR_HANDLE m_brdfRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_brdfSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_brdfSRVCpu;
    GeometryState m_integrateBRDFState;

    Platform::GPUResource m_lumTexture0;
    Platform::GPUResource m_lumTexture1;
    Point2i m_lum0Groups;
    Point2i m_lum1Groups;
    ID3D12PipelineState* m_pLuminancePSO;
    ID3D12RootSignature* m_pLuminanceRS;
    ID3D12PipelineState* m_pLuminanceFinalPSO;
    ID3D12RootSignature* m_pLuminanceFinalRS;
    Platform::GPUResource m_tonemapParams;

    GeometryState m_tonemapGeomState;

    double m_angle; // Current rotation angle for model

    GeometryState m_equirectToCubemapFaceState;
    GeometryState m_irradianceConvolutionState;
    GeometryState m_environmentConvolutionState;
    GeometryState m_simpleCopyState;

    float m_lastUpdateDelta;

    float m_brightLightBrightness;

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;

    SceneParameters m_sceneParams;

    float m_value;
    float m_color[3];

    std::vector<std::wstring> m_cubemapNamesToBeRendered;
    Platform::GPUResource m_textureForDelete;
    std::vector<std::string> m_loadedCubemaps;
    std::vector<const char*> m_menuCubemaps;

    bool m_firstFrame;

    Platform::TextDraw* m_pTextDraw;
    Platform::TextDraw::FontId m_fontId;
};
