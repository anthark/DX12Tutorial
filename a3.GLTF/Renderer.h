#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "PlatformCubemapBuilder.h"
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
        RenderModeNormals
    };

    enum BlurMode
    {
        Naive,
        Separated,
        Compute
    };

    struct LightParam
    {
        float intensity;
        Point3f color;
        Point2f inverseDirSphere;
        float distance;
    };

    // Scene setup
    float exposure;
    float bloomThreshold;
    float bloomRatio;
    bool showGrid;
    bool showCubemap;
    bool applyBloom;
    bool applySpecAA;
    RenderMode renderMode;
    BlurMode blurMode;
    int cubemapIdx;

    // Lights
    int activeLightCount;
    LightParam lights[4];

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
    static const UINT BRDFRes = 512;
    static const UINT BlurSteps = 10;
    static const UINT BlurStepsCompute = 3;

private:

    struct Geometry : BaseRenderer::Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objData); return &objData; }

        ObjectData objData;
    };

    struct Node
    {
        bool useMatrix;
        union 
        {
            Matrix4f matrix;
            struct {
                Point4f rotation;
                Point3f translation;
                Point3f scale;
            } transform;
        };

        Node(const Matrix4f& matrix = Matrix4f())
            : useMatrix(true)
            , matrix(matrix)
        {}

        Node(const Point4f& rotation, const Point3f& translation, const Point3f& scale)
            : useMatrix(false)
        {
            transform.rotation = rotation;
            transform.translation = translation;
            transform.scale = scale;
        }

        std::vector<int> children;
    };

    struct AnimationSampler
    {
        // Assumed it is always linear interpolation here
        std::vector<float> timeKeys;   // Time key values
        std::vector<Point4f> keys;      // Key values
    };

    struct AnimationChannel
    {
        enum Type
        {
            Rotation,
            Translation,
            Scale
        };

        int animSamplerIdx;
        int nodeIdx;
        Type type;
    };

private:
    void MeasureLuminance();

    bool CallPostProcess(GeometryState& state, D3D12_GPU_DESCRIPTOR_HANDLE srv = {});
    bool Tonemap(int finalBloomIdx);
    bool DetectFlares();
    bool GaussBlur(int& finalBloomIdx);

    void SetupLights(Lights* pLights);

    bool CreateHDRTexture();
    void DestroyHDRTexture();

    bool CreateBRDFTexture();
    void DestroyBRDFTexture();

    bool CreateComputePipeline();
    void DestroyComputePipeline();

    bool IsCreationFrame() const { return m_firstFrame; }

    bool HasModelToLoad(std::wstring& modelName);
    bool ProcessModelLoad();

    void IntegrateBRDF();

    bool LoadModel(const std::wstring& name, tinygltf::Model** ppModel);
    // GLTF model processing routines
    bool ScanNode(const tinygltf::Model& model, int nodeIdx, const std::vector<Platform::GPUResource>& textures);
    bool ScanTexture(const tinygltf::Image& image, bool srgb, Platform::GPUResource& texture);

    void UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent);
    void UpdateMatrices();

    void ApplyAnimation(float time);

private:
    std::vector<Geometry> m_geometries;
    std::vector<Geometry> m_blendGeometries;
    std::vector<Geometry> m_serviceGeometries;

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

    double m_angle; // Current rotation angle for model

    float m_lastUpdateDelta;

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;

    SceneParameters m_sceneParams;

    float m_value;
    float m_color[3];

    std::vector<const char*> m_menuCubemaps;

    int m_rootNodeIdx;
    std::vector<Node> m_nodes;
    std::vector<Matrix4f> m_nodeMatrices;
    std::vector<Matrix4f> m_nodeNormalMatrices;
    std::vector<Matrix4f> m_nodeInvBindMatrices;
    std::vector<int> m_jointIndices;

    float m_maxAnimationTime;
    float m_minAnimationTime;
    std::vector<AnimationSampler> m_animationSamplers;
    std::vector<AnimationChannel> m_animationChannels;

    float m_animationTime;

    std::queue<std::wstring> m_modelsToLoad;
    tinygltf::Model* m_pModel; // GLTF model to be loaded
    std::vector<Platform::GPUResource> m_modelTextures;
    std::vector<bool> m_modelSRGB;

    bool m_firstFrame;

    Platform::TextDraw* m_pTextDraw;
    Platform::TextDraw::FontId m_fontId;

    static const Platform::CubemapBuilder::InitParams& CubemapBuilderParams;
    Platform::CubemapBuilder* m_pCubemapBuilder;
};
