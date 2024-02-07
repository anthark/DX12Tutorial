#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "PlatformCubemapBuilder.h"
#include "PlatformModelLoader.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include "Object.h"
#include "Light.h"
#include "Lightgrid.h"
#include "LightGeometry.h"
#include "CubemapTestGeom.h"

#include <queue>

namespace tinygltf
{
class Model;
class Node;
struct Image;
}

struct Lights;

const UINT ShadowSplits = 4;

struct LightAnim
{
    float phase;
    float period;
    float amplitude;
};

struct SceneParameters
{
    enum RenderMode
    {
        RenderModeLighting = 0,
        RenderModeSSAOMask
    };

    enum ShadowMode
    {
        ShadowModeSimple = 0,
        ShadowModePSSM,
        ShadowModeCSM
    };

    enum SSAOMode
    {
        SSAOBasic = 0,
        SSAOHalfSphere,
        SSAOHalfSphereNoise,
        SSAOHalfSphereNoiseBlur
    };

    struct LightParam
    {
        int lightType;
        float intensity;
        Point3f color;
        Point2f inverseDirSphere;
        float distance;
        Point3f lookAt;

    private:
        Platform::Camera camera;
    };

    enum RenderArch
    {
        Forward = 0,
        Deferred = 1,
        ForwardPlus = 2
    };

    // Scene setup
    float exposure;
    bool showGrid;
    bool showCubemap;
    bool applyBloom;
    bool applySSAO;
    RenderMode renderMode;
    int cubemapIdx;
    Platform::GLTFModel* pPrevModel;
    Platform::GLTFModel* pModel;
    float modelCenterY;
    Point3f modelDir;
    float initialModelAngle;
    float modelRotateSpeed;
    int modelIdx;
    RenderArch renderArch;

    // SSAO setup
    int ssaoSamplesCount;
    int ssaoNoiseSize;
    float ssaoKernelRadius;
    SSAOMode ssaoMode;
    bool ssaoUseRange;

    bool animated;
    bool showGPUCounters;

    bool vsync;
    bool editMode;
    bool editAddLightMode;
    int lightIdx;
    Point3f lightPos;
    float lightPosDir;

    // Shadow setup
    float shadowAreaScale;
    bool pcf;
    float shadowSplitsDist[ShadowSplits];
    bool tintSplits;
    ShadowMode shadowMode;
    bool tintOutArea;
    bool useBias;
    bool useSlopeScale;

    // Local cubemaps setup
    bool showTestCubes;

    // Test sphere setup
    float sphereRoughness;
    float sphereMetalness;

    // Lights
    int activeLightCount;
    LightParam lights[MaxLights];
    LightAnim lightAnims[MaxLights - 1];
    bool deferredLightsTest;

    bool showMenu;

    SceneParameters();

    int AddRandomLight();
};

class LightObject
{
public:
    LightObject();

    void SetLookAt(const Point3f& p);
    void SetLatLon(float lat, float lon);
    void SetRect(const Point2f& bbMin, const Point2f& bbMax);
    void SetSplitRect(UINT splitIdx, const Point2f& bbMin, const Point2f& bbMax);
    const std::pair<Point2f, Point2f>& GetSplitRect(UINT splitIdx) const { return m_splitRects[splitIdx]; }

    inline const Platform::Camera& GetCamera() const { return m_camera; }

private:
    Platform::Camera m_camera;
    std::pair<Point2f, Point2f> m_splitRects[ShadowSplits];
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
    virtual bool OnKeyUp(int virtualKeyCode) override;

    virtual bool RenderScene(const Platform::Camera& camera) override;

protected:
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:

    enum RenderPass
    {
        RenderPassColor = 0,
        RenderPassZ,
        RenderPassCubemap,
        RenderPassGBuffer
    };

    struct TestGeometry : public Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objData); return &objData; }

        CubemapTestGeomData objData;
    };

    struct LightGeometry : public Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objData); return &objData; }

        LightGeometryData objData;
    };

private:
    static const Point4f WhiteColor;
    static const Point4f BackColor;
    static const Point4f BlackBackColor;
    static const DXGI_FORMAT HDRFormat;
    static const UINT BRDFRes = 512;
    static const UINT BlurSteps = 10;
    static const UINT BlurStepsCompute = 3;

    static const UINT ShadowMapSize = 4096;
    //static const UINT ShadowSplitMapSize = 2048;
    static const UINT ShadowSplitMapSize = 4096;

    static const float LocalCubemapSize;

private:
    void MeasureLuminance();

    bool CallPostProcess(GeometryState& state, D3D12_GPU_DESCRIPTOR_HANDLE srv = {});
    bool DrawPostProcessRect();
    bool Tonemap(int finalBloomIdx);
    bool DetectFlares();
    void GaussBlur(int& finalBloomIdx);

    bool BlitTexture(D3D12_GPU_DESCRIPTOR_HANDLE srcTexHandle);

    void PresetupLights();
    void SetupLights(Lights* pLights);
    void SetupLightsCull(LightsCull* pLightsCull);

    bool CreateLightgrid();
    bool UpdateLightGrid();
    void DestroyLightgrid();

    bool CreateHDRTexture();
    void DestroyHDRTexture();

    bool CreateSSAOTextures();
    void DestroySSAOTextures();

    bool CreateDeferredTextures();
    void DestroyDeferredTextures();

    bool CreateShadowMap();
    void DestroyShadowMap();

    bool CreateBRDFTexture();
    void DestroyBRDFTexture();

    bool CreateComputePipeline();
    void DestroyComputePipeline();

    bool IsCreationFrame() const { return m_firstFrame; }

    void IntegrateBRDF();

    bool CreateTerrainGeometry();
    bool CreatePlayerSphereGeometry();
    void SetCurrentModel(Platform::GLTFModel* pModel);
    float CalcModelAutoRotate(const Point3f& cameraDir, float deltaSec, Point3f& newModelDir) const;

    void RenderModel(const Platform::GLTFModel* pModel, bool opaque, const RenderPass& pass = RenderPassColor);
    void RenderModel(const Platform::GLTFModelInstance* pInst, bool opaque, const RenderPass& pass = RenderPassColor);

    Platform::GLTFModelInstance* CreateInstance(const Platform::GLTFModel* pModel);

    void LoadScene(Platform::ModelLoader* pSceneModelLoader);
    void SaveScene();

    void RenderShadows(SceneCommon* pSceneCommonCB);
    void PrepareColorPass(const Platform::Camera& camera, const D3D12_RECT& rect);

    bool CreateCubemapTests();

    void DeferredRenderGBuffer();

    bool CreateDeferredLightGeometry();

    void ForwardPlusRenderDepthPrepass();

    void LightCulling();

    void DrawCounters();

    bool SSAOMaskGeneration();
    bool SSAOMaskBlur();

    float Random(float minVal, float maxVal);
    void GenerateSSAOKernel(Point4f* pSamples, int sampleCount, Point4f* pNoise, int noiseSize, bool halfSphere);

private:
    std::vector<Platform::GLTFGeometry> m_serviceGeometries;
    std::vector<TestGeometry> m_cubemapTestGeometries;

    Platform::GPUResource m_hdrRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hdrSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrSRVCpu;

    Platform::GPUResource m_ssaoMaskRT;
    Platform::GPUResource m_ssaoMaskRTBlur;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ssaoMaskRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_ssaoMaskSRV;

    Platform::GPUResource m_GBufferAlbedoRT;
    Platform::GPUResource m_GBufferF0RT;
    Platform::GPUResource m_GBufferNormalRT;
    Platform::GPUResource m_GBufferEmissiveRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferAlbedoRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferF0RTV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferNormalRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferEmissiveRTV;

    Platform::GPUResource m_bloomRT[2];
    D3D12_CPU_DESCRIPTOR_HANDLE m_bloomRTV[2];
    D3D12_GPU_DESCRIPTOR_HANDLE m_bloomSRV[2];
    D3D12_CPU_DESCRIPTOR_HANDLE m_bloomSRVCpu[2];
    D3D12_GPU_DESCRIPTOR_HANDLE m_hdrBloomSRV[2];
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrBloomSRVCpu[2];

    Platform::GPUResource m_shadowMap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_shadowMapDSV;
    Platform::GPUResource m_shadowMapSplits;
    D3D12_CPU_DESCRIPTOR_HANDLE m_shadowMapSplitDSV[ShadowSplits];

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
    ID3D12PipelineState* m_pComputeBlurHorzPSO;
    ID3D12PipelineState* m_pComputeBlurVertPSO;
    ID3D12PipelineState* m_pComputeBlurHorz4PSO;
    ID3D12PipelineState* m_pComputeBlurVert4PSO;
    ID3D12RootSignature* m_pComputeBlurRS;
    Platform::GPUResource m_tonemapParams;

    GeometryState m_tonemapGeomState;

    GeometryState m_ssaoMaskState;

    GeometryState m_detectFlaresState;
    GeometryState m_gaussBlurNaive;
    GeometryState m_gaussBlurVertical;
    GeometryState m_gaussBlurHorizontal;

    GeometryState m_blitState;

    float m_lastUpdateDelta;

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;

    SceneParameters m_sceneParams;

    float m_value;
    float m_color[3];

    std::vector<const char*> m_menuCubemaps;
    std::vector<const char*> m_menuPlayerModels;
    std::vector<const char*> m_menuModels;

    bool m_firstFrame;

    Platform::TextDraw* m_pTextDraw;
    Platform::TextDraw::FontId m_fontId;
    Platform::TextDraw::FontId m_counterFontId;

    static const Platform::CubemapBuilder::InitParams& CubemapBuilderParams;
    Platform::CubemapBuilder* m_pCubemapBuilder;
    Platform::ModelLoader* m_pModelLoader;
    Platform::ModelLoader* m_pPlayerModelLoader;

    std::vector<const Platform::GLTFModelInstance*> m_currentModels;// Current models to be drawn

    Platform::GLTFModel* m_pTerrainModel;
    Platform::GLTFModel* m_pSphereModel;
    Platform::GLTFModelInstance* m_pModelInstance;

    LightGeometry* m_pFullScreenLight;
    LightGeometry* m_pPointLight;
    GeometryState  m_pointAltState;

    int m_rotationDir;
    float m_modelAngle;

    LightObject m_lights[MaxLights];

    Platform::GPUResource m_lightgridFrustums;
    Point2i m_lightGridCells;
    bool m_lightgridUpdateNeeded;
    Platform::GPUResource m_lightgridList;
    Platform::GPUResource m_zeroUintBuffer;
    Platform::GPUResource m_lightGrid;

    Platform::GPUResource m_minMaxDepth;
    ID3D12PipelineState* m_pMinMaxDepthPSO;
    ID3D12RootSignature* m_pMinMaxDepthRS;

    std::vector<std::pair<std::tstring, Platform::DeviceTimeQuery>> m_counters;
};
