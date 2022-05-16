#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "PlatformCubemapBuilder.h"
#include "PlatformModelLoader.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include "Object.h"

#include <queue>

namespace tinygltf
{
class Model;
class Node;
struct Image;
}

struct Lights;

const UINT ShadowSplits = 4;

struct SceneParameters
{
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
        RenderModeSpecularIBLBRDF,

        RenderModeAlbedo,
        RenderModeNormals,
    };

    enum ShadowMode
    {
        ShadowModeSimple = 0,
        ShadowModePSSM,
        ShadowModeCSM
    };

    struct LightParam
    {
        float intensity;
        Point3f color;
        Point2f inverseDirSphere;
        float distance;
        Point3f lookAt;

    private:
        Platform::Camera camera;
    };

    // Scene setup
    float exposure;
    bool showGrid;
    bool showCubemap;
    bool applyBloom;
    bool animated;
    RenderMode renderMode;
    int cubemapIdx;
    Platform::GLTFModel* pPrevModel;
    Platform::GLTFModel* pModel;
    float modelCenterY;
    Point3f modelDir;
    float initialModelAngle;
    float modelRotateSpeed;
    int modelIdx;

    bool editMode;

    // Shadow setup
    float shadowAreaScale;
    bool pcf;
    float shadowSplitsDist[ShadowSplits];
    bool tintSplits;
    ShadowMode shadowMode;
    bool tintOutArea;
    bool useBias;
    bool useSlopeScale;

    // Lights
    int activeLightCount;
    LightParam lights[4];

    bool showMenu;

    SceneParameters();
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

protected:
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:

    enum RenderPass
    {
        RenderPassColor = 0,
        RenderPassZ
    };

private:
    static const Point4f BackColor;
    static const DXGI_FORMAT HDRFormat;
    static const UINT BRDFRes = 512;
    static const UINT BlurSteps = 10;
    static const UINT BlurStepsCompute = 3;

    static const UINT ShadowMapSize = 4096;
    //static const UINT ShadowSplitMapSize = 2048;
    static const UINT ShadowSplitMapSize = 4096;

private:
    void MeasureLuminance();

    bool CallPostProcess(GeometryState& state, D3D12_GPU_DESCRIPTOR_HANDLE srv = {});
    bool Tonemap(int finalBloomIdx);
    bool DetectFlares();
    void GaussBlur(int& finalBloomIdx);

    void PresetupLights();
    void SetupLights(Lights* pLights);

    bool CreateHDRTexture();
    void DestroyHDRTexture();

    bool CreateShadowMap();
    void DestroyShadowMap();

    bool CreateBRDFTexture();
    void DestroyBRDFTexture();

    bool CreateComputePipeline();
    void DestroyComputePipeline();

    bool IsCreationFrame() const { return m_firstFrame; }

    void IntegrateBRDF();

    bool CreateTerrainGeometry();
    void SetCurrentModel(Platform::GLTFModel* pModel);
    float CalcModelAutoRotate(const Point3f& cameraDir, float deltaSec, Point3f& newModelDir) const;

    void RenderModel(const Platform::GLTFModel* pModel, bool opaque, const RenderPass& pass = RenderPassColor);
    void RenderModel(const Platform::GLTFModelInstance* pInst, bool opaque, const RenderPass& pass = RenderPassColor);

    Platform::GLTFModelInstance* CreateInstance(const Platform::GLTFModel* pModel);

    void LoadScene(Platform::ModelLoader* pSceneModelLoader);
    void SaveScene();

    void RenderShadows(SceneCommon* pSceneCommonCB);
    void PrepareColorPass();

private:
    std::vector<Platform::GLTFGeometry> m_serviceGeometries;

    Platform::GPUResource m_hdrRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hdrSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrSRVCpu;

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
    ID3D12RootSignature* m_pComputeBlurRS;
    Platform::GPUResource m_tonemapParams;

    GeometryState m_tonemapGeomState;

    GeometryState m_detectFlaresState;
    GeometryState m_gaussBlurNaive;
    GeometryState m_gaussBlurVertical;
    GeometryState m_gaussBlurHorizontal;

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

    static const Platform::CubemapBuilder::InitParams& CubemapBuilderParams;
    Platform::CubemapBuilder* m_pCubemapBuilder;
    Platform::ModelLoader* m_pModelLoader;
    Platform::ModelLoader* m_pPlayerModelLoader;

    bool m_modelsUpdated;                                   // If models for ddrawing are updated and geometries are to rebuilt
    std::vector<const Platform::GLTFModelInstance*> m_currentModels;// Current models to be drawn

    Platform::GLTFModel* m_pTerrainModel;
    Platform::GLTFModelInstance* m_pModelInstance;

    int m_rotationDir;
    float m_modelAngle;

    LightObject m_lights[4];
};
