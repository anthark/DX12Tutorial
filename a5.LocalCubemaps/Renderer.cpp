#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformTexture.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"
#include "PlatformCubemapBuilder.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

// Shader includes
#include "Light.h"
#include "Luminance.h"
#include "LuminanceFinal.h"
#include "Tonemap.h"
#include "EquirectToCubemap.h"

#include <chrono>

#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <algorithm>

namespace
{

struct ColorVertex
{
    Point3f pos;
    Point3f color;
};

struct TextureVertex
{
    Point3f pos;
    Point2f uv;
    Point3f normal;
};

struct TextVertex
{
    Point3f pos;
    Point3f color;
    Point2f uv;
};

struct PostprocessingVertex
{
    Point2f pos;
    Point2f uv;
};

const float BaseLow = 0.01f;

const std::vector<const char*> SceneGeometryTypeNames = {"Single object", "Objects grid"};
const std::vector<const char*> RenderModeNames = { "Lighting", "  Diffuse", "  IBL Diffuse", "  Specular", "    Normal Distribution", "    Geometry", "    Fresnel", "  IBL Environment", "  IBL Fresnel", "  IBL BRDF", "Albedo", "Normals" };
const std::vector<const char*> ShadowMapModeNames = { "Simple", "PSSM", "CSM" };

}

LightObject::LightObject()
    : m_camera()
{
    m_camera.SetLat(100.0f);
    m_camera.SetLon(100.0f);

    m_camera.SetProjection(Platform::Camera::Orthographic);
    m_camera.SetNear(0.0f);
    m_camera.SetFar(200.0f);
    m_camera.SetDistance(100.0f);

    m_camera.SetHorzScale(100.0f);
}

void LightObject::SetLatLon(float lat, float lon)
{
    if (fabs(m_camera.GetLat() - lat) > 0.0001f || fabs(m_camera.GetLon() - lon) > 0.0001f)
    {
        m_camera.SetLat(lat);
        m_camera.SetLon(lon);
    }
}

void LightObject::SetLookAt(const Point3f& p)
{
    if ((m_camera.GetLookAt() - p).length() > 0.0001f)
    {
        m_camera.SetLookAt(p);
    }
}

void LightObject::SetRect(const Point2f& bbMin, const Point2f& bbMax)
{
    m_camera.SetRect(bbMin.x, bbMin.y, bbMax.x, bbMax.y);
}

void LightObject::SetSplitRect(UINT splitIdx, const Point2f& bbMin, const Point2f& bbMax)
{
    assert(splitIdx <= ShadowSplits);

    m_splitRects[splitIdx] = std::make_pair(bbMin, bbMax);
}

SceneParameters::SceneParameters()
    : exposure(10.0f)
    , showGrid(false)
    , showCubemap(true)
    , applyBloom(true)
    , renderMode(RenderModeLighting)
    , cubemapIdx(0)
    , pPrevModel(nullptr)
    , pModel(nullptr)
    , initialModelAngle(-(float)M_PI / 2)
    , modelRotateSpeed(1.0f)
    , editMode(false)
    , modelIdx(0)
    , shadowAreaScale(10.0f)
    , pcf(true)
    , tintSplits(false)
    , shadowMode(ShadowModeCSM)
    , tintOutArea(false)
    , useBias(true)
    , useSlopeScale(true)
    , sphereRoughness(0.0001f)
    , sphereMetalness(0.631f)
    , showTestCubes(false)
    , animated(true)
    , bloomRatio(0.44f)
    , applySpecAA(true)
{
    showMenu = true;

    activeLightCount = 1;
    lights[0].color = Point3f{ 1,1,1 };
    lights[0].intensity = 10.0f;
    lights[0].distance = 5.0f;
    //lights[0].inverseDirSphere = Point2f{(float)(1.5 * M_PI), (float)(0.25 * M_PI) };
    lights[0].inverseDirSphere = Point2f{ 1.765f, 0.477f };

    shadowSplitsDist[0] = 10.0f;
    shadowSplitsDist[1] = 33.0f;
    shadowSplitsDist[2] = 100.0f;
    shadowSplitsDist[3] = 300.0f;
}

const Point4f Renderer::BackColor = Point4f{0.25f,0.25f,0.25f,1};
const DXGI_FORMAT Renderer::HDRFormat = DXGI_FORMAT_R11G11B10_FLOAT;
const float Renderer::LocalCubemapSize = 5.0f;
const int LocalCubemapIrradianceRes = 32;
const int LocalCubemapEnvironmentRes = 128;
const bool UseLocalCubemaps = true;

const Platform::CubemapBuilder::InitParams& Renderer::CubemapBuilderParams = {
    512,    // Cubemap resolution
    9,      // Cubemap mips
    32,     // Irradiance map resolution
    128,    // Environment map resolution
    5       // Roughness mips
};

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 2, 32, { sizeof(Matrix4f), sizeof(Lights) }, 1 + ShadowSplits + 1)
    , CameraControlEuler()
    , m_pTextDraw(nullptr)
    , m_fpsCount(0)
    , m_prevFPS(0.0)
    , m_fps(0.0)
    , m_pLuminancePSO(nullptr)
    , m_pLuminanceRS(nullptr)
    , m_pLuminanceFinalPSO(nullptr)
    , m_pLuminanceFinalRS(nullptr)
    , m_pComputeBlurHorzPSO(nullptr)
    , m_pComputeBlurVertPSO(nullptr)
    , m_pComputeBlurRS(nullptr)
    , m_lastUpdateDelta(0)
    , m_value(0)
    , m_pCubemapBuilder(nullptr)
    , m_pModelLoader(nullptr)
    , m_pPlayerModelLoader(nullptr)
    , m_modelsUpdated(false)
    , m_pTerrainModel(nullptr)
    , m_pSphereModel(nullptr)
    , m_pModelInstance(nullptr)
    , m_rotationDir(0)
    , m_modelAngle(0.0f)
{
    m_color[0] = m_color[1] = m_color[2] = 1.0f;
}

Renderer::~Renderer()
{
    assert(m_pTextDraw == nullptr);
    assert(m_pLuminancePSO == nullptr);
    assert(m_pLuminanceRS == nullptr);
    assert(m_pLuminanceFinalPSO == nullptr);
    assert(m_pLuminanceFinalRS == nullptr);
    assert(m_pComputeBlurHorzPSO == nullptr);
    assert(m_pComputeBlurVertPSO == nullptr);
    assert(m_pComputeBlurRS == nullptr);
    assert(m_pCubemapBuilder == nullptr);
    assert(m_pModelLoader == nullptr);
    assert(m_pPlayerModelLoader == nullptr);
    assert(m_pTerrainModel == nullptr);
    assert(m_pSphereModel == nullptr);
    assert(m_pModelInstance == nullptr);
}

bool Renderer::Init(HWND hWnd)
{
    m_firstFrame = true;

    static const int GridCells = 10;
    static const float GridStep = 1.0f;

    size_t vertexCount = 0;
    size_t indexCount = 0;
    std::vector<ColorVertex> gridVertices;
    std::vector<TextureVertex> sphereVertices;
    std::vector<UINT16> indices;

    bool res = Platform::BaseRenderer::Init(hWnd);
    if (res)
    {
        res = BeginGeometryCreation();

        if (res)
        {
            m_pTextDraw = new Platform::TextDraw();
            res = m_pTextDraw->Init(this);
            if (res)
            {
                m_pTextDraw->Resize(GetRect());
            }
        }
        if (res)
        {
            res = m_pTextDraw->CreateFont(_T("../Common/terminus.ttf"), 24, m_fontId);
        }

        if (res)
        {
            static const size_t SphereSteps = 64;

            Platform::GLTFGeometry geometry;
            CreateGeometryParams params;

            Platform::GetSphereDataSize(SphereSteps, SphereSteps, indexCount, vertexCount);
            sphereVertices.resize(vertexCount);
            indices.resize(indexCount);
            Platform::CreateSphere(SphereSteps, SphereSteps, indices.data(), sizeof(TextureVertex), &sphereVertices[0].pos, &sphereVertices[0].normal, &sphereVertices[0].uv);

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
            params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 20 });
            params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
            params.indexFormat = DXGI_FORMAT_R16_UINT;
            params.pIndices = indices.data();
            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("PBRMaterial.hlsl");
            params.pVertices = sphereVertices.data();
            params.vertexDataSize = (UINT)sphereVertices.size() * sizeof(TextureVertex);
            params.vertexDataStride = sizeof(TextureVertex);
            params.rtFormat = HDRFormat;
            params.geomStaticTexturesCount = 0;

            if (res)
            {
                params.pShaderSourceName = _T("Cubemap.hlsl");
                params.geomStaticTexturesCount = 0;

                params.geomStaticTextures.clear();
                params.rasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
                params.depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

                res = CreateGeometry(params, geometry);
                if (res)
                {
                    m_serviceGeometries.push_back(geometry);
                }

                params.rasterizerState.CullMode = D3D12_CULL_MODE_BACK;
                params.depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            }

            if (res)
            {
                geometry = Platform::GLTFGeometry();
                indices.clear();

                Platform::GetGridDataSize(GridCells, indexCount, vertexCount);
                gridVertices.resize(vertexCount);
                indices.resize(indexCount);
                Platform::CreateGrid(GridCells, GridStep, indices.data(), sizeof(ColorVertex), &gridVertices[0].pos);
                for (auto& vertex : gridVertices)
                {
                    vertex.color = Point3f{ 1,1,1 };
                }

                params.geomAttributes.clear();
                params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
                params.geomAttributes.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
                params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
                params.indexFormat = DXGI_FORMAT_R16_UINT;
                params.pIndices = indices.data();
                params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                params.pShaderSourceName = _T("SimpleColor.hlsl");
                params.pVertices = gridVertices.data();
                params.vertexDataSize = (UINT)gridVertices.size() * sizeof(ColorVertex);
                params.vertexDataStride = sizeof(ColorVertex);
                params.rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

                params.geomStaticTextures.clear();
                params.geomStaticTexturesCount = 0;

                res = CreateGeometry(params, geometry);
            }
            if (res)
            {
                m_serviceGeometries.push_back(geometry);
            }
        }
        if (res)
        {
            res = CreateTerrainGeometry();
        }
        if (res)
        {
            res = CreatePlayerSphereGeometry();
        }

        EndGeometryCreation();

        if (res)
        {
            res = GetDevice()->AllocateRenderTargetView(m_hdrRTV, 2);
            if (res)
            {
                res = GetDevice()->AllocateStaticDescriptors(2, m_hdrSRVCpu, m_hdrSRV);
            }
        }
        if (res)
        {
            for (int i = 0; i < 2 && res; i++)
            {
                res = GetDevice()->AllocateRenderTargetView(m_bloomRTV[i]);
                if (res)
                {
                    res = GetDevice()->AllocateStaticDescriptors(1, m_bloomSRVCpu[i], m_bloomSRV[i]);
                }
                if (res)
                {
                    res = GetDevice()->AllocateStaticDescriptors(2, m_hdrBloomSRVCpu[i], m_hdrBloomSRV[i]);
                }
            }
        }
        if (res)
        {
            res = CreateHDRTexture();
        }

        if (res)
        {
            res = CreateShadowMap();
        }

        if (res)
        {
            res = GetDevice()->AllocateRenderTargetView(m_brdfRTV);
            if (res)
            {
                res = GetDevice()->AllocateStaticDescriptors(1, m_brdfSRVCpu, m_brdfSRV);
            }
            if (res)
            {
                res = CreateBRDFTexture();
            }
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("IntegrateBRDF.hlsl");
            geomStateParams.geomStaticTexturesCount = 0;
            geomStateParams.rtFormat = DXGI_FORMAT_R32G32_FLOAT;

            res = CreateGeometryState(geomStateParams, m_integrateBRDFState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Tonemap.hlsl");
            geomStateParams.geomStaticTexturesCount = 2;
            geomStateParams.depthStencilState.DepthEnable = FALSE;

            res = CreateGeometryState(geomStateParams, m_tonemapGeomState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Bloom.hlsl");
            geomStateParams.shaderDefines.push_back("DETECT");
            geomStateParams.geomStaticTexturesCount = 2;
            geomStateParams.depthStencilState.DepthEnable = FALSE;
            geomStateParams.rtFormat = HDRFormat;

            geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;

            geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

            res = CreateGeometryState(geomStateParams, m_detectFlaresState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Bloom.hlsl");
            geomStateParams.shaderDefines.push_back("GAUSS_BLUR_NAIVE");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = FALSE;
            geomStateParams.rtFormat = HDRFormat;

            res = CreateGeometryState(geomStateParams, m_gaussBlurNaive);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Bloom.hlsl");
            geomStateParams.shaderDefines.push_back("GAUSS_BLUR_SEPARATED_VERTICAL");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = FALSE;
            geomStateParams.rtFormat = HDRFormat;

            res = CreateGeometryState(geomStateParams, m_gaussBlurVertical);
        }
        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Bloom.hlsl");
            geomStateParams.shaderDefines.push_back("GAUSS_BLUR_SEPARATED_HORIZONTAL");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = FALSE;
            geomStateParams.rtFormat = HDRFormat;

            res = CreateGeometryState(geomStateParams, m_gaussBlurHorizontal);
        }

        if (res)
        {
            res = CreateComputePipeline();
        }
    }

    if (res)
    {
        m_pCubemapBuilder = new Platform::CubemapBuilder();
        std::vector<std::tstring> hdrFiles = Platform::ScanFiles(_T("../Common"), _T("*.hdr"));
        res = m_pCubemapBuilder->Init(this, hdrFiles, CubemapBuilderParams);

        if (res)
        {
            m_pModelLoader = new Platform::ModelLoader();
            //std::vector<std::tstring> modelFiles = Platform::ScanDirectories(_T("../../Models"), _T("scene.gltf"));
            std::vector<std::tstring> modelFiles = Platform::ScanDirectories(_T("../Common/SceneModels"), _T("scene.gltf"));
            res = m_pModelLoader->Init(this, modelFiles, HDRFormat, DXGI_FORMAT_R32G32B32A32_FLOAT, false, false);
        }

        if (res)
        {
            m_pPlayerModelLoader = new Platform::ModelLoader();
            std::vector<std::tstring> modelFiles = Platform::ScanDirectories(_T("../Common/PlayerModels"), _T("scene.gltf"));
            m_pPlayerModelLoader->Init(this, modelFiles, HDRFormat, DXGI_FORMAT_R32G32B32A32_FLOAT, false, UseLocalCubemaps);
        }
    }

    if (res)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();

        res = ImGui_ImplWin32_Init(hWnd);

        if (res)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            res = GetDevice()->AllocateStaticDescriptors(1, cpuHandle, gpuHandle);

            if (res)
            {
                res = ImGui_ImplDX12_Init(GetDevice()->GetDXDevice(), (int)GetDevice()->GetFramesInFlight(), DXGI_FORMAT_R8G8B8A8_UNORM, GetDevice()->GetDescriptorHeap(), cpuHandle, gpuHandle);
            }
            if (res)
            {
                // Setup Dear ImGui style
                ImGui::StyleColorsDark();
            }
        }

        m_brdfReady = false;
    }

    return res;
}

void Renderer::Term()
{
    delete m_pModelInstance;
    m_pModelInstance = nullptr;

    SaveScene();

    for (auto& model : m_currentModels)
    {
        delete model;
    }
    m_currentModels.clear();

    GetDevice()->WaitGPUIdle();

    m_pTerrainModel->Term(this);
    delete m_pTerrainModel;
    m_pTerrainModel = nullptr;

    m_pSphereModel->Term(this);
    delete m_pSphereModel;
    m_pSphereModel = nullptr;

    TERM_RELEASE(m_pPlayerModelLoader);
    TERM_RELEASE(m_pModelLoader);
    TERM_RELEASE(m_pCubemapBuilder);
    TERM_RELEASE(m_pTextDraw);

    ImGui_ImplWin32_Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui::DestroyContext();

    DestroyComputePipeline();

    for (auto geometry : m_serviceGeometries)
    {
        DestroyGeometry(geometry);
    }
    m_serviceGeometries.clear();
    for (auto geometry : m_cubemapTestGeometries)
    {
        DestroyGeometry(geometry);
    }
    m_cubemapTestGeometries.clear();

    DestroyGeometryState(m_gaussBlurHorizontal);
    DestroyGeometryState(m_gaussBlurVertical);
    DestroyGeometryState(m_gaussBlurNaive);
    DestroyGeometryState(m_detectFlaresState);

    DestroyGeometryState(m_tonemapGeomState);
    DestroyGeometryState(m_integrateBRDFState);

    DestroyBRDFTexture();
    DestroyShadowMap();
    DestroyHDRTexture();

    Platform::BaseRenderer::Term();
}

static bool NearestDir(float angleStart, float angleEnd)
{
    if (angleStart > 0 && angleEnd > 0
        || angleStart < 0 && angleEnd < 0)
    {
        return angleEnd - angleStart > 0;
    }

    if (angleEnd <= 0)
    {
        float delta = angleEnd + 2 * (float)M_PI - angleStart;

        return delta < (float)M_PI;
    }

    float delta = angleEnd - angleStart;

    return delta < (float)M_PI;
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_lastUpdateDelta = (float)(deltaSec);

    Point3f cameraMoveDir = UpdateCamera(deltaSec);

    if (m_camera.GetLat() < 0.0f)
    {
        m_camera.SetLat(0.0f);
    }

    Point4f cameraPos = m_camera.CalcPos();
    Matrix4f trans;
    Matrix4f scale;
    scale.Scale(2.0f, 2.0f, 2.0f);
    trans.Offset(Point3f{cameraPos.x, cameraPos.y, cameraPos.z});
    m_serviceGeometries[0].splitData.transform = scale * trans;
    m_serviceGeometries[0].splitData.transformNormals = trans.Inverse().Transpose();

    if (m_pModelInstance != nullptr && m_pModelInstance->pModel == m_pSphereModel)
    {
        m_pModelInstance->instGeomData[0].pbr.x = m_sceneParams.sphereRoughness;
        m_pModelInstance->instGeomData[0].pbr.y = m_sceneParams.sphereMetalness;
    }

    if (m_sceneParams.editMode)
    {
        if (m_pModelInstance != nullptr)
        {
            float delta = (float)(deltaSec * m_sceneParams.modelRotateSpeed);
            m_modelAngle += delta * m_rotationDir;

            m_pModelInstance->SetPos(m_camera.GetLookAt() - Point3f{ 0, m_sceneParams.modelCenterY, 0 });
            m_pModelInstance->SetAngle(m_modelAngle);
        }
    }
    else
    {
        if (m_pModelInstance != nullptr)
        {
            // Apply animations here
            if (m_sceneParams.animated)
            {
                m_pModelInstance->animationTime += (float)deltaSec;
                m_pModelInstance->ApplyAnimation();
                m_pModelInstance->UpdateMatrices();
            }

            if (cameraMoveDir.lengthSqr() > 0.00001f)
            {
                Point3f newModelDir;
                float modelAngle = CalcModelAutoRotate(cameraMoveDir, (float)deltaSec, newModelDir);
                m_pModelInstance->SetPos(m_camera.GetLookAt() - Point3f{ 0, m_sceneParams.modelCenterY, 0 });
                m_pModelInstance->SetAngle(modelAngle);

                m_sceneParams.modelDir = newModelDir;
            }
        }
    }

    if (elapsedSec - m_prevFPS >= 1.0)
    {
        m_fps = m_fpsCount;
        m_fpsCount = 0;

        m_prevFPS = elapsedSec;
    }

    return true;
}

bool Renderer::Render()
{
    m_pTextDraw->ResetCaret();

    ++m_fpsCount;

    D3D12_RECT rect = GetRect();
    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    UINT8* dynCBData[2] = {};
    BeginRenderParams beginParams = {
        {BackColor.x, BackColor.y, BackColor.z, BackColor.w},
        dynCBData
    };
    std::wstring cubemapName;
    std::wstring modelName;
    if (BeginRender(beginParams))
    {
        if (IsCreationFrame())
        {
            IntegrateBRDF();

            m_firstFrame = false;
        }
        else
        {
            if (m_pPlayerModelLoader->HasModelsToLoad())
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Loading model: %ls"), GetParentName(m_pPlayerModelLoader->GetCurrentModelName()).c_str());

                m_pPlayerModelLoader->ProcessModelLoad();

                if (!m_pPlayerModelLoader->HasModelsToLoad())
                {
                    for (const auto& str : m_pPlayerModelLoader->GetLoadedModels())
                    {
                        m_menuPlayerModels.push_back(str.c_str());
                    }
                    m_menuPlayerModels.push_back("Sphere");

                    SetCurrentModel(0);

                    m_sceneParams.modelCenterY = (m_pPlayerModelLoader->GetModel(0)->bbMax.y - m_pPlayerModelLoader->GetModel(0)->bbMin.y) / 2;
                    m_sceneParams.modelDir = Point3f{0,0,-1};
                    m_camera.SetLookAt(Point3f{ 0, m_sceneParams.modelCenterY, 0 });
                }
            }
            else if (m_pModelLoader->HasModelsToLoad())
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Loading model: %ls"), GetParentName(m_pModelLoader->GetCurrentModelName()).c_str());

                m_pModelLoader->ProcessModelLoad();

                if (!m_pModelLoader->HasModelsToLoad())
                {
                    for (const auto& str : m_pModelLoader->GetLoadedModels())
                    {
                        m_menuModels.push_back(str.c_str());
                    }

                    LoadScene(m_pModelLoader);
                }
            }
            else if (m_pCubemapBuilder->HasCubemapsToBuild())
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Building cubemap: %ls"), m_pCubemapBuilder->GetCurrentCubemapName().c_str());

                m_pCubemapBuilder->RenderCubemap();

                if (!m_pCubemapBuilder->HasCubemapsToBuild())
                {
                    for (const auto& str : m_pCubemapBuilder->GetLoadedCubemaps())
                    {
                        m_menuCubemaps.push_back(str.c_str());
                    }
                }
            }
            else if (m_pCubemapBuilder->HasLocalCubemapsToBuild())
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Building local cubemap: %d/%d"), m_pCubemapBuilder->GetBuiltLocalCubemaps(), m_pCubemapBuilder->GetLocalCubemapsToBuild());

                std::tstring pixName = _T("LocalCubemap ");
                pixName += (_T('0') + 0);

                PIX_MARKER_SCOPE_STR(LocalCubemap, pixName.c_str());

                m_pCubemapBuilder->RenderLocalCubemap();

                if (!m_pCubemapBuilder->HasLocalCubemapsToBuild())
                {
                    m_sceneParams.activeLightCount = 1;

                    CreateCubemapTests();
                }
            }
            else if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
                && GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
            {
                // AAV TEMP
                /*{
                    std::tstring pixName = _T("LocalCubemap ");
                    pixName += (_T('0') + 0);

                    PIX_MARKER_SCOPE_STR(LocalCubemap, pixName.c_str());

                    m_pCubemapBuilder->RenderLocalCubemap();
                }*/

                PresetupLights();

                RenderShadows(reinterpret_cast<SceneCommon*>(dynCBData[0]));

                PrepareColorPass(*GetCamera(), GetRect());

                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDSVHandle();
                GetCurrentCommandList()->OMSetRenderTargets(2, &m_hdrRTV, TRUE, &dsvHandle);

                GetCurrentCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

                D3D12_VIEWPORT viewport = GetViewport();
                GetCurrentCommandList()->RSSetViewports(1, &viewport);
                GetCurrentCommandList()->RSSetScissorRects(1, &rect);

                FLOAT clearColor[4] = { BackColor.x, BackColor.y, BackColor.z, BackColor.w };
                GetCurrentCommandList()->ClearRenderTargetView(m_hdrRTV, clearColor, 1, &rect);

                FLOAT bloomClearColor[4] = { 0 };
                GetCurrentCommandList()->ClearRenderTargetView(m_bloomRTV[0], bloomClearColor, 1, &rect);

                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // Opaque
                {
                    PIX_MARKER_SCOPE(Opaque);

                    RenderModel(m_pTerrainModel, true);
                    for (size_t i = 0; i < m_currentModels.size(); i++)
                    {
                        RenderModel(m_currentModels[i], true);
                    }
                    if (m_pModelInstance != nullptr)
                    {
                        RenderModel(m_pModelInstance, true);
                    }
                }

                if (m_sceneParams.showCubemap)
                {
                    RenderGeometry(m_serviceGeometries[0]);
                }

                if (m_sceneParams.showTestCubes)
                {
                    for (auto& geom : m_cubemapTestGeometries)
                    {
                        RenderGeometry(geom);
                    }
                }

                // Transparent
                {
                    PIX_MARKER_SCOPE(Transparents);

                    for (size_t i = 0; i < m_currentModels.size(); i++)
                    {
                        RenderModel(m_currentModels[i], false);
                    }
                    if (m_pModelInstance != nullptr)
                    {
                        RenderModel(m_pModelInstance, false);
                    }
                }

                GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                SetBackBufferRT(); // Return current back buffer as render target

                MeasureLuminance();

                int finalBloomIdx = -1;
                {
                    GetCurrentCommandList()->OMSetRenderTargets(1, &m_bloomRTV[0], TRUE, nullptr);

                    GetDevice()->TransitResourceState(GetCurrentCommandList(), GetDepthBuffer().pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    DetectFlares();
                    GetDevice()->TransitResourceState(GetCurrentCommandList(), GetDepthBuffer().pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

                    GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                    GaussBlur(finalBloomIdx);

                    SetBackBufferRT();
                }

                Tonemap(finalBloomIdx);

                if (m_sceneParams.showGrid)
                {
                    RenderGeometry(m_serviceGeometries[1]);
                }

                if (m_sceneParams.showMenu)
                {
                    // render your GUI
                    ImGui::Begin("Scene parameters");
                    ImGui::Checkbox("EditMode", &m_sceneParams.editMode);
                    ImGui::Text("Scene");
                    ImGui::SliderFloat("Exposure", &m_sceneParams.exposure, 0.001f, 10.0f);
                    //ImGui::Checkbox("Show grid", &m_sceneParams.showGrid);
                    //ImGui::Checkbox("Show cubemap", &m_sceneParams.showCubemap);
                    ImGui::Checkbox("Apply bloom", &m_sceneParams.applyBloom);
                    //ImGui::SliderInt("Lights", &m_sceneParams.activeLightCount, 1, 4);
                    //ImGui::ListBox("Render mode", (int*)&m_sceneParams.renderMode, RenderModeNames.data(), (int)RenderModeNames.size());
                    ImGui::Checkbox("Animated", &m_sceneParams.animated);
                    if (m_sceneParams.editMode)
                    {
                        if (m_sceneParams.modelIdx >= m_menuModels.size())
                        {
                            m_sceneParams.modelIdx = (int)m_menuModels.size() - 1;
                        }
                        ImGui::ListBox("Model", &m_sceneParams.modelIdx, m_menuModels.data(), (int)m_menuModels.size());
                        m_sceneParams.pModel = m_pModelLoader->GetModel(m_sceneParams.modelIdx);
                    }
                    else
                    {
                        if (m_sceneParams.modelIdx >= m_menuPlayerModels.size())
                        {
                            m_sceneParams.modelIdx = (int)m_menuPlayerModels.size() - 1;
                        }
                        ImGui::ListBox("Model", &m_sceneParams.modelIdx, m_menuPlayerModels.data(), (int)m_menuPlayerModels.size());
                        m_sceneParams.pModel = m_sceneParams.modelIdx < m_pPlayerModelLoader->GetModelCount() ? m_pPlayerModelLoader->GetModel(m_sceneParams.modelIdx) : m_pSphereModel;
                    }
                    ImGui::ListBox("Cubemap", &m_sceneParams.cubemapIdx, m_menuCubemaps.data(), (int)m_menuCubemaps.size());
                    ImGui::Checkbox("Show local cubemaps", &m_sceneParams.showTestCubes);
                    ImGui::End();

                    for (int i = 0; i < m_sceneParams.activeLightCount; i++)
                    {
                        char buffer[128];
                        sprintf(buffer, "Light %d", i);
                        ImGui::Begin(buffer);
                        ImGui::SliderFloat("Theta", &m_sceneParams.lights[i].inverseDirSphere.x, 0.0f, (float)(2.0 * M_PI));
                        ImGui::SliderFloat("Phi", &m_sceneParams.lights[i].inverseDirSphere.y, -(float)(0.5 * M_PI), (float)(0.5 * M_PI));
                        ImGui::SliderFloat("Distance", &m_sceneParams.lights[i].distance, 0.01f, 100.0f);
                        ImGui::SliderFloat("Intensity", &m_sceneParams.lights[i].intensity, 1.0f, 500.0f);
                        ImGui::ColorEdit3("Color", (float*)&m_sceneParams.lights[i].color);
                        ImGui::End();
                    }

                    // Shadow setup
                    {
                        ImGui::Begin("Shadow setup");
                        ImGui::ListBox("Shadow mode", (int*)&m_sceneParams.shadowMode, ShadowMapModeNames.data(), (int)ShadowMapModeNames.size());
                        ImGui::Checkbox("Use bias", &m_sceneParams.useBias);
                        ImGui::Checkbox("Use slope scale bias", &m_sceneParams.useSlopeScale);
                        switch (m_sceneParams.shadowMode)
                        {
                            case SceneParameters::ShadowModeSimple:
                                ImGui::SliderFloat("Shadow area", &m_sceneParams.shadowAreaScale, 10.0f, 100.0f);
                                ImGui::Checkbox("Tint outside area", &m_sceneParams.tintOutArea);
                                break;

                            case SceneParameters::ShadowModePSSM:
                                ImGui::Checkbox("Tint splits", &m_sceneParams.tintSplits);
                                break;

                            case SceneParameters::ShadowModeCSM:
                                ImGui::Checkbox("Tint splits", &m_sceneParams.tintSplits);
                                break;
                        }
                        ImGui::Checkbox("Use PCF", &m_sceneParams.pcf);
                        ImGui::End();
                    }

                    // Test sphere setup
                    if (m_sceneParams.pModel == m_pSphereModel)
                    {
                        ImGui::Begin("Test sphere setup");
                        ImGui::SliderFloat("Roughness", &m_sceneParams.sphereRoughness, 0.0001f, 1.0f);
                        ImGui::SliderFloat("Metalness", &m_sceneParams.sphereMetalness, 0.0f, 1.0f);
                        ImGui::End();
                    }
                }

                // Render dear imgui into screen
                ImGui::Render();
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandList());

                // Reset state to ours after ImGui draw
                ResetRender();

                m_pTextDraw->DrawText(m_fontId, Point3f{1,1,1}, _T("FPS: %5.2f"), m_fps);

                if (m_sceneParams.pPrevModel != m_sceneParams.pModel)
                {
                    SetCurrentModel(m_sceneParams.pModel);
                    m_sceneParams.pPrevModel = m_sceneParams.pModel;
                }
            }
        }

        EndRender();

        return true;
    }

    return false;
}

bool Renderer::OnKeyDown(int virtualKeyCode)
{
    bool update = false;
    switch (virtualKeyCode)
    {
        case 192: // Tilda
            {
                m_sceneParams.showMenu = !m_sceneParams.showMenu;
            }
            break;

        case ' ': // Break
            {
                if (m_sceneParams.editMode && m_pModelInstance != nullptr)
                {
                    Platform::GLTFModelInstance* pNewInstance = new Platform::GLTFModelInstance();
                    *pNewInstance = *m_pModelInstance;

                    m_currentModels.push_back(pNewInstance);
                }
            }
            break;

        case 'Q':
        case 'q':
            m_rotationDir += 1;
            update = true;
            break;

        case 'E':
        case 'e':
            m_rotationDir -= 1;
            update = true;
            break;
    }

    return update ? update : CameraControlEuler::OnKeyDown(virtualKeyCode);
}

bool Renderer::OnKeyUp(int virtualKeyCode)
{
    bool update = false;
    switch (virtualKeyCode)
    {
        case 'Q':
        case 'q':
            m_rotationDir -= 1;
            update = true;
            break;

        case 'E':
        case 'e':
            m_rotationDir += 1;
            update = true;
            break;
    }

    return update ? update : CameraControlEuler::OnKeyUp(virtualKeyCode);
}

bool Renderer::RenderScene(const Platform::Camera& camera)
{
    D3D12_RECT rect;
    rect.top = rect.left = 0;
    rect.right = rect.bottom = 512;

    PrepareColorPass(camera, rect);

    m_sceneParams.activeLightCount = 0;

    // Opaque
    {
        PIX_MARKER_SCOPE(Opaque);

        RenderModel(m_pTerrainModel, true, RenderPassCubemap);
        for (size_t i = 0; i < m_currentModels.size(); i++)
        {
            RenderModel(m_currentModels[i], true, RenderPassCubemap);
        }
    }

    return true;
}

bool Renderer::Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect)
{
    bool res = BaseRenderer::Resize(viewport, rect);

    if (res)
    {
        DestroyHDRTexture();
        res = CreateHDRTexture();

        if (res)
        {
            m_pTextDraw->Resize(rect);
        }
    }

    return res;
}

void Renderer::MeasureLuminance()
{
    GetCurrentCommandList()->SetPipelineState(m_pLuminancePSO);
    GetCurrentCommandList()->SetComputeRootSignature(m_pLuminanceRS);

    struct MeasureLuminanceStep
    {
        ID3D12Resource* pSrcTexture;
        ID3D12Resource* pDstTexture;
        Point2i groups;
    };

    MeasureLuminanceStep steps[2] = {
        {m_hdrRT.pResource, m_lumTexture0.pResource, m_lum0Groups},
        {m_lumTexture0.pResource, m_lumTexture1.pResource, m_lum1Groups}
    };

    // Two steps here
    for (int i = 0; i < sizeof(steps)/sizeof(MeasureLuminanceStep); i++)
    {
        D3D12_RESOURCE_DESC srcDesc = steps[i].pSrcTexture->GetDesc();

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

        GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        // Setup constant buffer with parameters
        LuminanceParams* pLuminanceParams = nullptr;
        UINT64 paramsGPUAddress;
        GetDevice()->AllocateDynamicBuffer(sizeof(LuminanceParams), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, (void**)&pLuminanceParams, paramsGPUAddress);

        pLuminanceParams->srcTextureSize = Point2i{ (int)srcDesc.Width, (int)srcDesc.Height };
        pLuminanceParams->step0 = i == 0 ? 1 : 0;

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = paramsGPUAddress;
        cbvDesc.SizeInBytes = Align((int)sizeof(LuminanceParams), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        GetDevice()->GetDXDevice()->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup src texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = srcDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(steps[i].pSrcTexture, &srvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup dst texture
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = steps[i].pDstTexture->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        GetDevice()->GetDXDevice()->CreateUnorderedAccessView(steps[i].pDstTexture, nullptr, &uavDesc, cpuHandle);

        // Set parameters
        GetCurrentCommandList()->SetComputeRootDescriptorTable(0, gpuHandle);

        GetCurrentCommandList()->Dispatch(steps[i].groups.x, steps[i].groups.y, 1);
    }

    GetCurrentCommandList()->SetPipelineState(m_pLuminanceFinalPSO);
    GetCurrentCommandList()->SetComputeRootSignature(m_pLuminanceFinalRS);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

        GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        // Setup constant buffer with parameters
        LuminanceFinalParams* pLuminanceFinalParams = nullptr;
        UINT64 paramsGPUAddress;
        GetDevice()->AllocateDynamicBuffer(sizeof(LuminanceFinalParams), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, (void**)&pLuminanceFinalParams, paramsGPUAddress);

        pLuminanceFinalParams->time = Point4f{m_lastUpdateDelta};

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = paramsGPUAddress;
        cbvDesc.SizeInBytes = Align((int)sizeof(LuminanceFinalParams), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        GetDevice()->GetDXDevice()->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup src texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = m_lumTexture1.pResource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_lumTexture1.pResource, &srvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup dst buffer
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.NumElements = 1;
        uavDesc.Buffer.StructureByteStride = sizeof(TonemapParams);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        GetDevice()->GetDXDevice()->CreateUnorderedAccessView(m_tonemapParams.pResource, nullptr, &uavDesc, cpuHandle);

        // Set parameters
        GetCurrentCommandList()->SetComputeRootDescriptorTable(0, gpuHandle);

        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_tonemapParams.pResource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        GetCurrentCommandList()->Dispatch(1, 1, 1);

        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_tonemapParams.pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
}

bool Renderer::CallPostProcess(GeometryState& state, D3D12_GPU_DESCRIPTOR_HANDLE srv)
{
    UINT64 gpuVirtualAddress;

    PostprocessingVertex* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(4 * sizeof(PostprocessingVertex));
    bool res = GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        // Fill
        pVertices[0].pos = Point2f{ -1, -1 };
        pVertices[1].pos = Point2f{ 1, -1 };
        pVertices[2].pos = Point2f{ 1,  1 };
        pVertices[3].pos = Point2f{ -1,  1 };

        pVertices[0].uv = Point2f{ 0, 1 };
        pVertices[1].uv = Point2f{ 1, 1 };
        pVertices[2].uv = Point2f{ 1, 0 };
        pVertices[3].uv = Point2f{ 0, 0 };

        // Setup
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        vertexBufferView.BufferLocation = gpuVirtualAddress;
        vertexBufferView.StrideInBytes = (UINT)sizeof(PostprocessingVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(6 * sizeof(UINT16));
        res = GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
        if (res)
        {
            // Fill
            pIndices[0] = 0;
            pIndices[1] = 2;
            pIndices[2] = 1;
            pIndices[3] = 0;
            pIndices[4] = 3;
            pIndices[5] = 2;

            // Setup
            D3D12_INDEX_BUFFER_VIEW indexBufferView;

            indexBufferView.BufferLocation = gpuVirtualAddress;
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            GetCurrentCommandList()->IASetIndexBuffer(&indexBufferView);
        }
    }

    if (res)
    {
        SetupGeometryState(state);

        GetCurrentCommandList()->SetGraphicsRootConstantBufferView(1, m_tonemapParams.pResource->GetGPUVirtualAddress());

        GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, srv.ptr == 0 ? m_hdrSRV : srv);

        GetCurrentCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    return res;
}

bool Renderer::Tonemap(int finalBloomIdx)
{
    return CallPostProcess(m_tonemapGeomState, m_hdrBloomSRV[finalBloomIdx]);
}

bool Renderer::DetectFlares()
{
    return CallPostProcess(m_detectFlaresState);
}

void Renderer::GaussBlur(int& finalBloomIdx)
{
    for (int i = 0; i < BlurStepsCompute; i++)
    {
        GetCurrentCommandList()->SetPipelineState(m_pComputeBlurHorzPSO);
        GetCurrentCommandList()->SetComputeRootSignature(m_pComputeBlurRS);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

        GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        // Setup constant buffer with parameters
        Point2i* pImageSize = nullptr;
        UINT64 paramsGPUAddress;
        GetDevice()->AllocateDynamicBuffer(sizeof(LuminanceFinalParams), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, (void**)&pImageSize, paramsGPUAddress);

        RECT rect = GetRect();
        Point2i imageSize = Point2i{ rect.right - rect.left, rect.bottom - rect.top };
        *pImageSize = imageSize;

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = paramsGPUAddress;
        cbvDesc.SizeInBytes = Align((int)sizeof(Point2i), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        GetDevice()->GetDXDevice()->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup src texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = m_bloomRT[0].pResource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_bloomRT[0].pResource, &srvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup dst buffer
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = m_bloomRT[1].pResource->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        GetDevice()->GetDXDevice()->CreateUnorderedAccessView(m_bloomRT[1].pResource, nullptr, &uavDesc, cpuHandle);

        // Set parameters
        GetCurrentCommandList()->SetComputeRootDescriptorTable(0, gpuHandle);

        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[1].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        GetCurrentCommandList()->Dispatch(30, 17, 1);
        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[1].pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // Second (vertical) step
        GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        GetCurrentCommandList()->SetPipelineState(m_pComputeBlurVertPSO);
        GetCurrentCommandList()->SetComputeRootSignature(m_pComputeBlurRS);

        // Setup constant buffer with parameters
        cbvDesc.BufferLocation = paramsGPUAddress;
        cbvDesc.SizeInBytes = Align((int)sizeof(Point2i), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        GetDevice()->GetDXDevice()->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup src texture
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = m_bloomRT[1].pResource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_bloomRT[1].pResource, &srvDesc, cpuHandle);

        cpuHandle.ptr += GetDevice()->GetSRVDescSize();

        // Setup dst buffer
        uavDesc.Format = m_bloomRT[0].pResource->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        GetDevice()->GetDXDevice()->CreateUnorderedAccessView(m_bloomRT[0].pResource, nullptr, &uavDesc, cpuHandle);

        // Set parameters
        GetCurrentCommandList()->SetComputeRootDescriptorTable(0, gpuHandle);

        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        GetCurrentCommandList()->Dispatch(30, 17, 1);
        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    finalBloomIdx = 0;
}

void Renderer::PresetupLights()
{
    // Convert from light params to scene light objects
    for (int i = 0; i < m_sceneParams.activeLightCount; i++)
    {
        const auto& light = m_sceneParams.lights[i];

        m_lights[i].SetLatLon(light.inverseDirSphere.y, light.inverseDirSphere.x);
        m_lights[i].SetLookAt(GetCamera()->GetLookAt());

        float scale = m_sceneParams.shadowAreaScale * 0.5f;
        m_lights[i].SetRect(Point2f{ -scale, -scale }, Point2f{ scale, scale });
    }

    if (m_sceneParams.shadowMode == SceneParameters::ShadowModeSimple)
    {
        float scale = m_sceneParams.shadowAreaScale * 0.5f;
        m_lights[0].SetSplitRect(0, Point2f{ -scale, -scale }, Point2f{ scale, scale });
    }
    else if (m_sceneParams.shadowMode == SceneParameters::ShadowModePSSM)
    {
        // Setup light shadow cameras
        D3D12_RECT rect = GetRect();
        float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

        float nearPlane = GetCamera()->GetNear();
        for (int i = 0; i < 4; i++)
        {
            float farPlane = m_sceneParams.shadowSplitsDist[i];

            std::vector<Point3f> pts;
            GetCamera()->CalcFrustumPoints(pts, nearPlane, farPlane, aspectRatioHdivW);

            // Calculate light space bounding box for this split frustum
            Point3f right, up, dir;
            m_lights[0].GetCamera().CalcDirection(right, up, dir);
            Point4f pos4 = m_lights[0].GetCamera().CalcPos();
            Point3f pos{ pos4.x, pos4.y, pos4.z };

            Point3f splitBBMin{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            Point3f splitBBMax{ std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min() };
            for (size_t j = 0; j < pts.size(); j++)
            {
                float x = (pts[j] - pos).dot(right);
                float y = (pts[j] - pos).dot(up);
                float z = (pts[j] - pos).dot(dir);

                splitBBMin.x = std::min(splitBBMin.x, x);
                splitBBMin.y = std::min(splitBBMin.y, y);
                splitBBMin.z = std::min(splitBBMin.z, z);

                splitBBMax.x = std::max(splitBBMax.x, x);
                splitBBMax.y = std::max(splitBBMax.y, y);
                splitBBMax.z = std::max(splitBBMax.z, z);
            }

            float lightNear = splitBBMin.z;
            float lightFar = splitBBMax.z;

            float lightWidth = splitBBMax.x - splitBBMin.x;
            float lightHeight = splitBBMax.y - splitBBMin.y;

            float maxSize = std::max(lightHeight, lightWidth);
            splitBBMax.x = splitBBMin.x + maxSize;
            splitBBMax.y = splitBBMin.y + maxSize;

            m_lights[0].SetSplitRect(i, Point2f{ splitBBMin.x,splitBBMin.y }, Point2f{ splitBBMax.x,splitBBMax.y });

            nearPlane = farPlane;
        }
    }
    else if (m_sceneParams.shadowMode == SceneParameters::ShadowModeCSM)
    {
        for (int i = 0; i < 4; i++)
        {
            float scale = m_sceneParams.shadowSplitsDist[i] * 0.5f;
            m_lights[0].SetSplitRect(i, Point2f{ -scale, -scale }, Point2f{ scale, scale });
        }
    }
    else
    {
        assert(0);
    }
}

void Renderer::SetupLights(Lights* pLights)
{
    // Setup lights constant buffer
    pLights->ambientColor = Point3f{ 0.1f, 0.1f, 0.1f };

    for (int i = 0; i < m_sceneParams.activeLightCount; i++)
    {
        const auto& light = m_sceneParams.lights[i];

        pLights->lights[i].type = LT_Direction;
        Point3f right, dir, up;
        m_lights[i].GetCamera().CalcDirection(right, up, dir);
        pLights->lights[i].dir = Point4f{ dir.x, dir.y, dir.z, 0 };
        pLights->lights[i].color = Point4f{ light.color.x, light.color.y, light.color.z } * light.intensity;

        // Matrix to transform from (-1,-1)x(1,1) to (0,0)x(1,1)
        Matrix4f uvTrans = Matrix4f().Scale(0.5, -0.5, 1) * Matrix4f().Offset(Point3f{ 0.5, 0.5, 0 });

        if (i == 0)
        {
            float prev = m_sceneParams.shadowSplitsDist[0];
            for (int j = 0; j < ShadowSplits; j++)
            {
                const auto& splitRect = m_lights[i].GetSplitRect(j);
                m_lights[i].SetRect(splitRect.first, splitRect.second);

                Matrix4f lightSpace = m_lights[i].GetCamera().CalcViewMatrix();
                Matrix4f lightProj = m_lights[i].GetCamera().CalcProjMatrix(1.0f);

                pLights->lights[i].worldToLight[j] = lightSpace * lightProj * uvTrans;

                prev = m_sceneParams.shadowSplitsDist[j];
            }
            pLights->lights[i].csmRatio = Point4f{
                1.0f, 
                m_sceneParams.shadowSplitsDist[0] / m_sceneParams.shadowSplitsDist[1],
                m_sceneParams.shadowSplitsDist[1] / m_sceneParams.shadowSplitsDist[2],
                m_sceneParams.shadowSplitsDist[2] / m_sceneParams.shadowSplitsDist[3]
            };
        }
    }
    pLights->lightCount = m_sceneParams.activeLightCount;
}

bool Renderer::CreateHDRTexture()
{
    assert(m_hdrRT.pResource == nullptr);

    UINT height = GetRect().bottom - GetRect().top;
    UINT width = GetRect().right - GetRect().left;

    Platform::CreateTextureParams params;
    params.format = HDRFormat;
    params.height = height;
    params.width = width;
    params.enableRT = true;
    params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = params.format;
    memcpy(&clearValue.Color, &BackColor, sizeof(BackColor));
    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, GetDevice(), m_hdrRT);

    if (res)
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = params.format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;

        GetDevice()->GetDXDevice()->CreateRenderTargetView(m_hdrRT.pResource, &desc, m_hdrRTV);
    }
    if (res)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.Format = m_hdrRT.pResource->GetDesc().Format;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_hdrRT.pResource, &texDesc, m_hdrSRVCpu);
    }
    if (res)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE depthSRVCpu = m_hdrSRVCpu;
        depthSRVCpu.ptr += GetDevice()->GetSRVDescSize();

        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(GetDepthBuffer().pResource, &texDesc, depthSRVCpu);
    }

    if (res)
    {
        m_lum0Groups = Point2i{(int)DivUp(width, (UINT)(LuminanceGroupSize * 2)), (int)DivUp(height, (UINT)(LuminanceGroupSize * 2)) };

        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R11G11B10_FLOAT;
        params.height = m_lum0Groups.y;
        params.width = m_lum0Groups.x;
        params.enableUAV = true;
        params.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        res = Platform::CreateTexture(params, false, GetDevice(), m_lumTexture0);
    }
    if (res)
    {
        m_lum1Groups = Point2i{ DivUp(m_lum0Groups.x, LuminanceGroupSize * 2), DivUp(m_lum0Groups.y, LuminanceGroupSize * 2) };

        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = m_lum1Groups.y;
        params.width = m_lum1Groups.x;
        params.enableUAV = true;
        params.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        res = Platform::CreateTexture(params, false, GetDevice(), m_lumTexture1);
    }

    // Create additional textures for bloom
    if (res)
    {
        D3D12_CLEAR_VALUE clearValue;
        clearValue.Format = params.format;
        memset(&clearValue.Color, 0, sizeof(float) * 4); // Black
        params.pOptimizedClearValue = &clearValue;
        params.enableUAV = true;
        for (int i = 0; i < 2 && res; i++)
        {
            res = Platform::CreateTexture(params, false, GetDevice(), m_bloomRT[i]);
            if (res)
            {
                D3D12_RENDER_TARGET_VIEW_DESC desc;
                desc.Format = params.format;
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = 0;
                desc.Texture2D.PlaneSlice = 0;

                GetDevice()->GetDXDevice()->CreateRenderTargetView(m_bloomRT[i].pResource, &desc, m_bloomRTV[i]);
            }
            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_bloomRT[i].pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_bloomRT[i].pResource, &texDesc, m_bloomSRVCpu[i]);
            }
            if (res)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE srv = m_hdrBloomSRVCpu[i];

                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_hdrRT.pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_hdrRT.pResource, &texDesc, srv);

                srv.ptr += GetDevice()->GetSRVDescSize();
                texDesc.Format = m_bloomRT[i].pResource->GetDesc().Format;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_bloomRT[i].pResource, &texDesc, srv);
            }
        }

        if (res)
        {
            D3D12_RENDER_TARGET_VIEW_DESC desc;
            desc.Format = params.format;
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = 0;
            desc.Texture2D.PlaneSlice = 0;

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_hdrRTV;
            rtv.ptr += GetDevice()->GetRTVDescSize();
            GetDevice()->GetDXDevice()->CreateRenderTargetView(m_bloomRT[0].pResource, &desc, rtv);
        }
    }

    return res;
}

void Renderer::DestroyHDRTexture()
{
    for (int i = 0; i < 2; i++)
    {
        GetDevice()->ReleaseGPUResource(m_bloomRT[i]);
    }

    GetDevice()->ReleaseGPUResource(m_lumTexture1);
    GetDevice()->ReleaseGPUResource(m_lumTexture0);
    GetDevice()->ReleaseGPUResource(m_hdrRT);
}

bool Renderer::CreateShadowMap()
{
    assert(m_shadowMap.pResource == nullptr);

    Platform::CreateTextureParams params;
    params.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    params.height = ShadowMapSize;
    params.width = ShadowMapSize;
    params.enableDS = true;
    params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = params.format;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;
    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, GetDevice(), m_shadowMap);

    if (res)
    {
        m_shadowMapDSV = GetDSVStartHandle();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
        dsvView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvView.Flags = D3D12_DSV_FLAG_NONE;

        GetDevice()->GetDXDevice()->CreateDepthStencilView(m_shadowMap.pResource, &dsvView, m_shadowMapDSV);
    }

    // Create splitted shadowmap
    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        params.height = ShadowSplitMapSize;
        params.width = ShadowSplitMapSize;
        params.enableDS = true;
        params.arraySize = ShadowSplits;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        D3D12_CLEAR_VALUE clearValue;
        clearValue.Format = params.format;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        params.pOptimizedClearValue = &clearValue;

        res = Platform::CreateTexture(params, false, GetDevice(), m_shadowMapSplits);
    }
    if (res)
    {
        m_shadowMapSplits.pResource->SetName(_T("Splits shadow map"));

        D3D12_CPU_DESCRIPTOR_HANDLE splitDSV = GetDSVStartHandle();
        splitDSV.ptr += GetDevice()->GetDSVDescSize();
        for (UINT i = 0; i < ShadowSplits; i++)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
            dsvView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvView.Flags = D3D12_DSV_FLAG_NONE;
            dsvView.Texture2DArray.ArraySize = 1;
            dsvView.Texture2DArray.FirstArraySlice = i;
            dsvView.Texture2DArray.MipSlice = 0;

            GetDevice()->GetDXDevice()->CreateDepthStencilView(m_shadowMapSplits.pResource, &dsvView, splitDSV);

            m_shadowMapSplitDSV[i] = splitDSV;

            splitDSV.ptr += GetDevice()->GetDSVDescSize();
        }
    }

    return res;
}

void Renderer::DestroyShadowMap()
{
    GetDevice()->ReleaseGPUResource(m_shadowMapSplits);
    GetDevice()->ReleaseGPUResource(m_shadowMap);
}

bool Renderer::CreateBRDFTexture()
{
    assert(m_brdf.pResource == nullptr);

    Platform::CreateTextureParams params;
    params.format = DXGI_FORMAT_R32G32_FLOAT;
    params.height = BRDFRes;
    params.width = BRDFRes;
    params.enableRT = true;
    params.initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = params.format;
    memcpy(&clearValue.Color, &BackColor, sizeof(BackColor));
    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, GetDevice(), m_brdf);

    if (res)
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = params.format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;

        GetDevice()->GetDXDevice()->CreateRenderTargetView(m_brdf.pResource, &desc, m_brdfRTV);
    }
    if (res)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.Format = m_brdf.pResource->GetDesc().Format;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_brdf.pResource, &texDesc, m_brdfSRVCpu);
    }

    return res;
}

void Renderer::DestroyBRDFTexture()
{
    GetDevice()->ReleaseGPUResource(m_brdf);
}

bool Renderer::CreateComputePipeline()
{
    assert(m_pLuminancePSO == nullptr);

    ID3D12Device* pDevice = GetDevice()->GetDXDevice();

    bool res = true;
    // Luminance measurement
    if (res)
    {
        std::vector<D3D12_ROOT_PARAMETER> rootSignatureParams;

        D3D12_DESCRIPTOR_RANGE descRanges[3] = {};
        descRanges[0].BaseShaderRegister = 0;
        descRanges[0].NumDescriptors = 1;
        descRanges[0].OffsetInDescriptorsFromTableStart = 0;
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRanges[0].RegisterSpace = 0;

        descRanges[1].BaseShaderRegister = 0;
        descRanges[1].NumDescriptors = 1;
        descRanges[1].OffsetInDescriptorsFromTableStart = 1;
        descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRanges[1].RegisterSpace = 0;

        descRanges[2].BaseShaderRegister = 0;
        descRanges[2].NumDescriptors = 1;
        descRanges[2].OffsetInDescriptorsFromTableStart = 2;
        descRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descRanges[2].RegisterSpace = 0;
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 3;
            param.DescriptorTable.pDescriptorRanges = descRanges;

            rootSignatureParams.push_back(param);
        }

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init((UINT)rootSignatureParams.size(), rootSignatureParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &m_pLuminanceRS);
    }
    ID3DBlob* pComputeShaderBinary = nullptr;
    if (res)
    {
        // Create shader
        res = GetDevice()->CompileShader(_T("Luminance.hlsl"), {}, Platform::Device::Compute, &pComputeShaderBinary);
    }
    if (res)
    {
        // Describe and create the graphics pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pLuminanceRS;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(pComputeShaderBinary);

        ID3D12Device* pDevice = GetDevice()->GetDXDevice();
        HRESULT hr = S_OK;
        D3D_CHECK(pDevice->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pLuminancePSO));

        res = SUCCEEDED(hr);
    }
    D3D_RELEASE(pComputeShaderBinary);

    // Final luminance measurement
    if (res)
    {
        std::vector<D3D12_ROOT_PARAMETER> rootSignatureParams;

        D3D12_DESCRIPTOR_RANGE descRanges[3] = {};
        descRanges[0].BaseShaderRegister = 0;
        descRanges[0].NumDescriptors = 1;
        descRanges[0].OffsetInDescriptorsFromTableStart = 0;
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRanges[0].RegisterSpace = 0;

        descRanges[1].BaseShaderRegister = 0;
        descRanges[1].NumDescriptors = 1;
        descRanges[1].OffsetInDescriptorsFromTableStart = 1;
        descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRanges[1].RegisterSpace = 0;

        descRanges[2].BaseShaderRegister = 0;
        descRanges[2].NumDescriptors = 1;
        descRanges[2].OffsetInDescriptorsFromTableStart = 2;
        descRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descRanges[2].RegisterSpace = 0;
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 3;
            param.DescriptorTable.pDescriptorRanges = descRanges;

            rootSignatureParams.push_back(param);
        }

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init((UINT)rootSignatureParams.size(), rootSignatureParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &m_pLuminanceFinalRS);
    }
    pComputeShaderBinary = nullptr;
    if (res)
    {
        // Create shader
        res = GetDevice()->CompileShader(_T("LuminanceFinal.hlsl"), {}, Platform::Device::Compute, &pComputeShaderBinary);
    }
    if (res)
    {
        // Describe and create the graphics pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pLuminanceFinalRS;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(pComputeShaderBinary);

        ID3D12Device* pDevice = GetDevice()->GetDXDevice();
        HRESULT hr = S_OK;
        D3D_CHECK(pDevice->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pLuminanceFinalPSO));

        res = SUCCEEDED(hr);
    }
    D3D_RELEASE(pComputeShaderBinary);

    // Compute Gauss blur
    if (res)
    {
        std::vector<D3D12_ROOT_PARAMETER> rootSignatureParams;

        D3D12_DESCRIPTOR_RANGE descRanges[3] = {};
        descRanges[0].BaseShaderRegister = 0;
        descRanges[0].NumDescriptors = 1;
        descRanges[0].OffsetInDescriptorsFromTableStart = 0;
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRanges[0].RegisterSpace = 0;

        descRanges[1].BaseShaderRegister = 32;
        descRanges[1].NumDescriptors = 1;
        descRanges[1].OffsetInDescriptorsFromTableStart = 1;
        descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRanges[1].RegisterSpace = 0;

        descRanges[2].BaseShaderRegister = 0;
        descRanges[2].NumDescriptors = 1;
        descRanges[2].OffsetInDescriptorsFromTableStart = 2;
        descRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descRanges[2].RegisterSpace = 0;
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 3;
            param.DescriptorTable.pDescriptorRanges = descRanges;

            rootSignatureParams.push_back(param);
        }

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init((UINT)rootSignatureParams.size(), rootSignatureParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &m_pComputeBlurRS);
    }
    if (res)
    {
        // Create shader
        res = GetDevice()->CompileShader(_T("Bloom.hlsl"), { "GAUSS_BLUR_COMPUTE", "GAUSS_BLUR_COMPUTE_HORZ" }, Platform::Device::Compute, &pComputeShaderBinary);
    }
    if (res)
    {
        // Describe and create the graphics pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pComputeBlurRS;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(pComputeShaderBinary);

        ID3D12Device* pDevice = GetDevice()->GetDXDevice();
        HRESULT hr = S_OK;
        D3D_CHECK(pDevice->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pComputeBlurHorzPSO));

        res = SUCCEEDED(hr);
    }
    D3D_RELEASE(pComputeShaderBinary);
    if (res)
    {
        // Create shader
        res = GetDevice()->CompileShader(_T("Bloom.hlsl"), { "GAUSS_BLUR_COMPUTE", "GAUSS_BLUR_COMPUTE_VERT"}, Platform::Device::Compute, &pComputeShaderBinary);
    }
    if (res)
    {
        // Describe and create the graphics pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pComputeBlurRS;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(pComputeShaderBinary);

        ID3D12Device* pDevice = GetDevice()->GetDXDevice();
        HRESULT hr = S_OK;
        D3D_CHECK(pDevice->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pComputeBlurVertPSO));

        res = SUCCEEDED(hr);
    }
    D3D_RELEASE(pComputeShaderBinary);

    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(sizeof(TonemapParams), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, m_tonemapParams);
    }

    return res;
}

void Renderer::DestroyComputePipeline()
{
    GetDevice()->ReleaseGPUResource(m_tonemapParams);

    D3D_RELEASE(m_pComputeBlurHorzPSO);
    D3D_RELEASE(m_pComputeBlurVertPSO);
    D3D_RELEASE(m_pComputeBlurRS);
    D3D_RELEASE(m_pLuminanceFinalPSO);
    D3D_RELEASE(m_pLuminanceFinalRS);
    D3D_RELEASE(m_pLuminancePSO);
    D3D_RELEASE(m_pLuminanceRS);
}

void Renderer::IntegrateBRDF()
{
    if (m_brdfReady)
    {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDSVHandle();
    GetCurrentCommandList()->OMSetRenderTargets(1, &m_brdfRTV, TRUE, &dsvHandle);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = (FLOAT)BRDFRes;
    viewport.Height = (FLOAT)BRDFRes;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    GetCurrentCommandList()->RSSetViewports(1, &viewport);

    D3D12_RECT rect = {};
    rect.bottom = BRDFRes;
    rect.right = BRDFRes;
    GetCurrentCommandList()->RSSetScissorRects(1, &rect);

    FLOAT clearColor[4] = { BackColor.x, BackColor.y, BackColor.z, BackColor.w };
    GetCurrentCommandList()->ClearRenderTargetView(m_brdfRTV, clearColor, 1, &rect);
    GetCurrentCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

    Point4f vertices[] = {
        {-1, -1, 0},
        { 1, -1, 0},
        { 1,  1, 0},
        {-1,  1, 0}
    };
    UINT16 indices[] = {
        0,2,1,0,3,2
    };

    UINT64 gpuVirtualAddress;

    Point4f* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(4 * sizeof(Point4f));
    bool res = GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        memcpy(pVertices, vertices, 4 * sizeof(Point4f));

        // Setup
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        vertexBufferView.BufferLocation = gpuVirtualAddress;
        vertexBufferView.StrideInBytes = (UINT)sizeof(Point4f);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(6 * sizeof(UINT16));
        res = GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
        if (res)
        {
            memcpy(pIndices, indices, 6 * sizeof(UINT16));

            // Setup
            D3D12_INDEX_BUFFER_VIEW indexBufferView;

            indexBufferView.BufferLocation = gpuVirtualAddress;
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            GetCurrentCommandList()->IASetIndexBuffer(&indexBufferView);
        }
    }
    if (res)
    {
        SetupGeometryState(m_integrateBRDFState);

        GetCurrentCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
    if (res)
    {
        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_brdf.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    m_brdfReady = res;
}

bool Renderer::CreateTerrainGeometry()
{
    static const std::tstring materialName = _T("MossyGround");

    Platform::GPUResource texture;
    Platform::GPUResource metalRoughTexture;
    Platform::GPUResource normalMapTexture;
    bool res = Platform::CreateTextureFromFile((_T("../Common/Textures/Terrain/") + materialName + _T("Albedo.png")).c_str(), GetDevice(), texture, true);
    if (res)
    {
        res = Platform::CreateTextureFromFile((_T("../Common/Textures/Terrain/") + materialName + _T("MetalRough.png")).c_str(), GetDevice(), metalRoughTexture);
    }
    if (res)
    {
        res = Platform::CreateTextureFromFile((_T("../Common/Textures/Terrain/") + materialName + _T("Normal.png")).c_str(), GetDevice(), normalMapTexture);
    }

    if (!res)
    {
        return res;
    }

    struct NormalVertex
    {
        Point3f pos;
        Point3f normal;
        Point3f tangent;
        Point2f uv;
    };

    Platform::GLTFGeometry* pTerrainGeometry = new Platform::GLTFGeometry();

    m_pTerrainModel = new Platform::GLTFModel();
    m_pTerrainModel->geometries.push_back(pTerrainGeometry);
    pTerrainGeometry->splitData.flags.x = 1;

    std::vector<NormalVertex> vertices(4);
    std::vector<UINT16> indices(6);

    vertices[0].pos = Point3f{ -100, 0, -100 };
    vertices[1].pos = Point3f{  100, 0, -100 };
    vertices[2].pos = Point3f{  100, 0,  100 };
    vertices[3].pos = Point3f{ -100, 0,  100 };

    vertices[0].uv = Point2f{ 0,0 };
    vertices[1].uv = Point2f{ 50,0 };
    vertices[2].uv = Point2f{ 50,50 };
    vertices[3].uv = Point2f{ 0,50 };

    for (int i = 0; i < 4; i++)
    {
        vertices[i].normal = Point3f{ 0, 1, 0 };
        vertices[i].tangent = Point3f{ 0, 0, 1 };
    }

    indices[0] = 0;
    indices[1] = 2;
    indices[2] = 1;
    indices[3] = 0;
    indices[4] = 3;
    indices[5] = 2;

    CreateGeometryParams params;

    params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
    params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
    params.geomAttributes.push_back({ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 24 });
    params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 36 });

    params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
    params.indexFormat = DXGI_FORMAT_R16_UINT;
    params.pIndices = indices.data();

    params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    params.pShaderSourceName = _T("Material.hlsl");
    params.pVertices = vertices.data();
    params.vertexDataSize = (UINT)vertices.size() * sizeof(NormalVertex);
    params.vertexDataStride = sizeof(NormalVertex);
    params.rtFormat = HDRFormat;
    params.rtFormat2 = HDRFormat;

    params.geomStaticTexturesCount = 4;
    params.geomStaticTextures.push_back(texture.pResource);
    params.geomStaticTextures.push_back(metalRoughTexture.pResource);
    params.geomStaticTextures.push_back(normalMapTexture.pResource);
    params.geomStaticTextures.push_back(texture.pResource);

    params.shaderDefines.push_back("NORMAL_MAP");
    //params.shaderDefines.push_back("PLAIN_METAL_ROUGH");

    res = CreateGeometry(params, *pTerrainGeometry);
    if (res)
    {
        pTerrainGeometry->splitData.metalF0 = Point4f{ 1,1,1,1 };
        pTerrainGeometry->splitData.pbr = Point4f{1, 1, 0.04f, 0};

        m_pTerrainModel->modelTextures.push_back(texture);
        m_pTerrainModel->modelTextures.push_back(metalRoughTexture);
        m_pTerrainModel->modelTextures.push_back(normalMapTexture);
    }

    if (res)
    {
        // Cubemap state
        params.shaderDefines.push_back("NO_BLOOM");
        params.shaderDefines.push_back("NO_POINT_LIGHTS");
        params.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.rtFormat2 = DXGI_FORMAT_UNKNOWN;

        BaseRenderer::GeometryState* pState = new BaseRenderer::GeometryState();
        res = CreateGeometryState(params, *pState);
        if (res)
        {
            m_pTerrainModel->cubePassStates.push_back(pState);
        }
    }

    return res;
}

bool Renderer::CreatePlayerSphereGeometry()
{
    bool res = true;

    struct NormalVertex
    {
        Point3f pos;
        Point3f normal;
        Point2f uv;
    };

    Platform::GLTFGeometry* pSphereGeometry = new Platform::GLTFGeometry();

    m_pSphereModel = new Platform::GLTFModel();
    m_pSphereModel->geometries.push_back(pSphereGeometry);
    pSphereGeometry->splitData.flags.x = 1;

    static const size_t SphereSteps = 64;

    std::vector<NormalVertex> sphereVertices;
    std::vector<UINT16> indices;

    size_t indexCount;
    size_t vertexCount;
    Platform::GetSphereDataSize(SphereSteps, SphereSteps, indexCount, vertexCount);
    sphereVertices.resize(vertexCount);
    indices.resize(indexCount);
    Platform::CreateSphere(SphereSteps, SphereSteps, indices.data(), sizeof(NormalVertex), &sphereVertices[0].pos, &sphereVertices[0].normal, &sphereVertices[0].uv);
    for (auto& vertex : sphereVertices)
    {
        vertex.pos.y += 0.5;
    }

    CreateGeometryParams params;

    params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
    params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
    params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 24 });

    params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
    params.indexFormat = DXGI_FORMAT_R16_UINT;
    params.pIndices = indices.data();

    params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    params.pShaderSourceName = _T("Material.hlsl");
    params.pVertices = sphereVertices.data();
    params.vertexDataSize = (UINT)sphereVertices.size() * sizeof(NormalVertex);
    params.vertexDataStride = sizeof(NormalVertex);
    params.rtFormat = HDRFormat;
    params.rtFormat2 = HDRFormat;

    params.geomStaticTexturesCount = 0;
    params.shaderDefines.push_back("PLAIN_METAL_ROUGH");
    params.shaderDefines.push_back("PLAIN_COLOR");

    if (UseLocalCubemaps)
    {
        params.shaderDefines.push_back("USE_LOCAL_CUBEMAPS");
    }

    res = CreateGeometry(params, *pSphereGeometry);
    if (res)
    {
        pSphereGeometry->splitData.metalF0 = Point4f{ 1,1,1,1 };
        pSphereGeometry->splitData.pbr = Point4f{ 1, 1, 0.04f, 0 };
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams zParams = params;
        zParams.pShaderSourceName = _T("ZPass.hlsl");
        zParams.shaderDefines.clear();

        params.shaderDefines.push_back("PLAIN_METAL_ROUGH");
        params.shaderDefines.push_back("PLAIN_COLOR");

        zParams.geomStaticTexturesCount = 1;
        zParams.blendState.RenderTarget[0].BlendEnable = FALSE;
        zParams.rtFormat = DXGI_FORMAT_UNKNOWN;
        zParams.rtFormat2 = DXGI_FORMAT_UNKNOWN;

        Platform::ZPassState state;

        BaseRenderer::GeometryState* pZState = new BaseRenderer::GeometryState();
        res = CreateGeometryState(zParams, *pZState);
        if (res)
        {
            state.states[Platform::ZPassTypeSimple] = pZState;
        }
        if (res)
        {
            pZState = new BaseRenderer::GeometryState();
            zParams.rasterizerState.DepthBias = 32;
            res = CreateGeometryState(zParams, *pZState);
            state.states[Platform::ZPassTypeBias] = pZState;
        }
        if (res)
        {
            pZState = new BaseRenderer::GeometryState();
            zParams.rasterizerState.SlopeScaledDepthBias = sqrtf(2.0f) * 2.0f;
            res = CreateGeometryState(zParams, *pZState);
            state.states[Platform::ZPassTypeBiasSlopeScale] = pZState;
        }
        if (res)
        {
            m_pSphereModel->zPassGeomStates.push_back(state);
        }
    }

    return res;
}

void Renderer::SetCurrentModel(Platform::GLTFModel* pModel)
{
    if (pModel != nullptr)
    {
        if (m_pModelInstance != nullptr)
        {
            delete m_pModelInstance;
            m_pModelInstance = nullptr;
        }

        Point3f lookAt = m_camera.GetLookAt();

        m_pModelInstance = CreateInstance(pModel);
        m_pModelInstance->SetPos(Point3f{lookAt.x, 0, lookAt.z});
        for (auto& data : m_pModelInstance->instGeomData)
        {
            data.flags.x = 1;
        }

        m_sceneParams.modelCenterY = (pModel->bbMax.y - pModel->bbMin.y) / 2;
        //m_sceneParams.modelDir = Point3f{ 0,0,-1 };
        lookAt.y = m_sceneParams.modelCenterY;
        m_camera.SetLookAt(lookAt);

        m_pModelInstance->animationTime = 0.0f;
    }
    m_modelsUpdated = true;
}

float Renderer::CalcModelAutoRotate(const Point3f& cameraDir, float deltaSec, Point3f& newModelDir) const
{
    float modelAngle = atan2f(m_sceneParams.modelDir.z, m_sceneParams.modelDir.x);
    float cameraAngle = atan2f(cameraDir.z, cameraDir.x);
    float angleDelta = cameraAngle - modelAngle;
    float delta = deltaSec * m_sceneParams.modelRotateSpeed * std::min(std::max(1.0f, m_camera.GetDistance()), (float)M_PI);
    if (fabs(angleDelta) < delta)
    {
        modelAngle = cameraAngle;
    }
    else
    {
        if (NearestDir(modelAngle, cameraAngle))
        {
            modelAngle += delta;
        }
        else
        {
            modelAngle -= delta;
        }
    }
    newModelDir.z = sinf(modelAngle);
    newModelDir.x = cosf(modelAngle);

    modelAngle = modelAngle - m_sceneParams.initialModelAngle;

    return modelAngle;
}

void Renderer::RenderModel(const Platform::GLTFModel* pModel, bool opaque, const RenderPass& pass)
{
    const auto& geometries = opaque ? pModel->geometries : pModel->blendGeometries;

    for (size_t i = 0; i < geometries.size(); i++)
    {
        GeometryState* pState = nullptr;
        if (pass == RenderPassZ)
        {
            pState = pModel->zPassGeomStates[i].states[Platform::ZPassTypeSimple];
            if (m_sceneParams.useBias)
            {
                pState = pModel->zPassGeomStates[i].states[Platform::ZPassTypeBias];
            }
            if (m_sceneParams.useSlopeScale)
            {
                pState = pModel->zPassGeomStates[i].states[Platform::ZPassTypeBiasSlopeScale];
            }
        }
        else if (pass == RenderPassCubemap)
        {
            pState = pModel->cubePassStates[i];
        }

        RenderGeometry(*geometries[i], nullptr, 0, pState, {}, &pModel->objData, sizeof(Platform::GLTFObjectData));
    }
}

void Renderer::RenderModel(const Platform::GLTFModelInstance* pInst, bool opaque, const RenderPass& pass)
{
    const auto& geometries = opaque ? pInst->pModel->geometries : pInst->pModel->blendGeometries;
    const auto& data = opaque ? pInst->instGeomData : pInst->instBlendGeomData;

    for (size_t i = 0; i < geometries.size(); i++)
    {
        GeometryState* pState = nullptr;
        if (pass == RenderPassZ)
        {
            pState = pInst->pModel->zPassGeomStates[i].states[Platform::ZPassTypeSimple];
            if (m_sceneParams.useBias)
            {
                pState = pInst->pModel->zPassGeomStates[i].states[Platform::ZPassTypeBias];
            }
            if (m_sceneParams.useSlopeScale)
            {
                pState = pInst->pModel->zPassGeomStates[i].states[Platform::ZPassTypeBiasSlopeScale];
            }
        }
        else if (pass == RenderPassCubemap)
        {
            pState = pInst->pModel->cubePassStates[i];
        }

        RenderGeometry(*geometries[i], &data[i], sizeof(Platform::GLTFSplitData), pState, {}, &pInst->instObjData, sizeof(Platform::GLTFObjectData));
    }
}

Platform::GLTFModelInstance* Renderer::CreateInstance(const Platform::GLTFModel* pModel)
{
    Platform::GLTFModelInstance* pInstance = new Platform::GLTFModelInstance();
    pInstance->pModel = pModel;
    pInstance->pos = Point3f{ 0, 0, 0 };

    pInstance->instGeomData.resize(pModel->geometries.size());
    for (int j = 0; j < pModel->geometries.size(); j++)
    {
        pInstance->instGeomData[j] = pModel->geometries[j]->splitData;
    }
    pInstance->instBlendGeomData.resize(pModel->blendGeometries.size());
    for (int j = 0; j < pModel->blendGeometries.size(); j++)
    {
        pInstance->instBlendGeomData[j] = pModel->blendGeometries[j]->splitData;
    }

    pInstance->instObjData = pModel->objData;

    pInstance->nodes = pModel->nodes;

    return pInstance;
}

void Renderer::LoadScene(Platform::ModelLoader* pSceneModelLoader)
{
    FILE* pFile = fopen("scene.bin", "rb");
    if (pFile == nullptr)
    {
        return;
    }

    UINT8 version = 0;
    fread(&version, sizeof(version), 1, pFile);
    assert(version == 1);

    while (!feof(pFile))
    {
        UINT32 len = 0;
        if (fread(&len, sizeof(len), 1, pFile) == 0) // EOF
        {
            break;
        }
        std::vector<wchar_t> buffer(len + 1, 0);
        fread(buffer.data(), sizeof(wchar_t), len, pFile);

        std::wstring modelName = buffer.data();
        Point3f pos;
        float angle;

        fread(&pos, sizeof(pos), 1, pFile);
        fread(&angle, sizeof(angle), 1, pFile);

        Platform::GLTFModel* pModel = pSceneModelLoader->FindModel(modelName);
        assert(pModel != nullptr);
        if (pModel != nullptr)
        {
            Platform::GLTFModelInstance* pInst = CreateInstance(pModel);
            pInst->SetPos(pos);
            pInst->SetAngle(angle);
            for (auto& data : pInst->instGeomData)
            {
                data.flags.x = 1;
            }
            for (auto& data : pInst->instBlendGeomData)
            {
                data.flags.x = 1;
            }
            

            m_currentModels.push_back(pInst);
        }
    }

    fclose(pFile);

    if (UseLocalCubemaps)
    {
        // Process scene to calculate overall bounding box
        AABB<float> sceneBB;
        for (size_t i = 0; i < m_currentModels.size(); i++)
        {
            Matrix4f instBB = m_currentModels[i]->CalcTransform();
            sceneBB.Add(instBB * m_currentModels[i]->pModel->bbMin);
            sceneBB.Add(instBB * m_currentModels[i]->pModel->bbMax);
        }

        // For empty scene
        if (sceneBB.IsEmpty())
        {
            sceneBB.Add(Point3f{ 0,0,0 });
        }

        Point2i sceneSize = Point2i{
            (int)(sceneBB.bbMax.x - sceneBB.bbMin.x + LocalCubemapSize - 1) / (int)LocalCubemapSize + 1,
            (int)(sceneBB.bbMax.y - sceneBB.bbMin.y + LocalCubemapSize - 1) / (int)LocalCubemapSize + 1
        };

        Platform::CubemapBuilder::InitLocalParams initParams;
        initParams.pos = Point2f{ sceneBB.bbMin.x - LocalCubemapSize / 2, sceneBB.bbMin.z - LocalCubemapSize / 2 };
        initParams.size = LocalCubemapSize;
        initParams.grid = sceneSize;

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDSVStartHandle();
        dsvHandle.ptr += (1 + ShadowSplits) * GetDevice()->GetDSVDescSize();

        initParams.dsvHandle = dsvHandle;

        initParams.irradianceRes = LocalCubemapIrradianceRes;
        initParams.envRes = LocalCubemapEnvironmentRes;
        initParams.roughnessMips = CubemapBuilderParams.roughnessMips;

        m_pCubemapBuilder->InitLocal(initParams);
    }
}

void Renderer::SaveScene()
{
    FILE* pFile = fopen("scene.bin", "wb");
    assert(pFile != nullptr);
    if (pFile != nullptr)
    {
        UINT8 version = 1;
        fwrite(&version, sizeof(version), 1, pFile);

        for (size_t i = 0; i < m_currentModels.size(); i++)
        {
            UINT32 len = (UINT32)m_currentModels[i]->pModel->name.length();
            fwrite(&len, sizeof(len), 1, pFile);
            fwrite(m_currentModels[i]->pModel->name.data(), sizeof(wchar_t), len, pFile);

            fwrite(&m_currentModels[i]->pos, sizeof(m_currentModels[i]->pos), 1, pFile);
            fwrite(&m_currentModels[i]->angle, sizeof(m_currentModels[i]->angle), 1, pFile);
        }

        fclose(pFile);
    }
}

void Renderer::RenderShadows(SceneCommon* pSceneCommonCB)
{
    PIX_MARKER_SCOPE(RenderShadows);

    if (m_sceneParams.shadowMode == SceneParameters::ShadowModeSimple)
    {
        if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_shadowMap.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE))
        {
            GetCurrentCommandList()->OMSetRenderTargets(0, nullptr, TRUE, &m_shadowMapDSV);

            D3D12_RECT rect;
            rect.left = rect.top = 0;
            rect.right = rect.bottom = ShadowMapSize;
            GetCurrentCommandList()->ClearDepthStencilView(m_shadowMapDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

            D3D12_VIEWPORT viewport;
            viewport.TopLeftX = viewport.TopLeftY = 0.0f;
            viewport.Height = (float)ShadowMapSize;
            viewport.Width = (float)ShadowMapSize;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            GetCurrentCommandList()->RSSetViewports(1, &viewport);
            GetCurrentCommandList()->RSSetScissorRects(1, &rect);

            pSceneCommonCB->VP = m_lights[0].GetCamera().CalcViewMatrix() * m_lights[0].GetCamera().CalcProjMatrix(1.0f);

            for (size_t i = 0; i < m_currentModels.size(); i++)
            {
                RenderModel(m_currentModels[i], true, RenderPassZ);
            }
            if (m_pModelInstance != nullptr)
            {
                RenderModel(m_pModelInstance, true, RenderPassZ);
            }

            GetDevice()->TransitResourceState(GetCurrentCommandList(), m_shadowMap.pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }
    else
    {
        for (UINT j = 0; j < ShadowSplits; j++)
        {
            std::tstring pixName = _T("Split ");
            pixName += (_T('0') + j);

            PIX_MARKER_SCOPE_STR(ShadowSplit, pixName.c_str());

            if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_shadowMapSplits.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE, j))
            {
                GetCurrentCommandList()->OMSetRenderTargets(0, nullptr, TRUE, &m_shadowMapSplitDSV[j]);

                D3D12_RECT rect;
                rect.left = rect.top = 0;
                rect.right = rect.bottom = ShadowSplitMapSize;
                GetCurrentCommandList()->ClearDepthStencilView(m_shadowMapSplitDSV[j], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &rect);

                D3D12_VIEWPORT viewport;
                viewport.TopLeftX = viewport.TopLeftY = 0.0f;
                viewport.Height = (float)ShadowSplitMapSize;
                viewport.Width = (float)ShadowSplitMapSize;
                viewport.MinDepth = 0.0f;
                viewport.MaxDepth = 1.0f;
                GetCurrentCommandList()->RSSetViewports(1, &viewport);
                GetCurrentCommandList()->RSSetScissorRects(1, &rect);

                auto const& splitRect = m_lights[0].GetSplitRect(j);
                m_lights[0].SetRect(splitRect.first, splitRect.second);

                pSceneCommonCB->VP = m_lights[0].GetCamera().CalcViewMatrix() * m_lights[0].GetCamera().CalcProjMatrix(1.0f);

                for (size_t i = 0; i < m_currentModels.size(); i++)
                {
                    RenderModel(m_currentModels[i], true, RenderPassZ);
                }
                if (m_pModelInstance != nullptr)
                {
                    RenderModel(m_pModelInstance, true, RenderPassZ);
                }

                GetDevice()->TransitResourceState(GetCurrentCommandList(), m_shadowMapSplits.pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, j);
            }

            UINT8* dynCBData[2] = {};
            BeginRenderParams beginParams = {
                {BackColor.x, BackColor.y, BackColor.z, BackColor.w},
                dynCBData
            };
            RestartCommonResources(beginParams);

            pSceneCommonCB = reinterpret_cast<SceneCommon*>(dynCBData[0]);
        }
    }

    auto const& splitRect = m_lights[0].GetSplitRect(0);
    m_lights[0].SetRect(splitRect.first, splitRect.second);
}

void Renderer::PrepareColorPass(const Platform::Camera& camera, const D3D12_RECT& rect)
{
    UINT8* dynCBData[2] = {};
    BeginRenderParams beginParams = {
        {BackColor.x, BackColor.y, BackColor.z, BackColor.w},
        dynCBData
    };

    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    RestartCommonResources(beginParams);

    // Setup VP matrix
    SceneCommon* pCommonCB = reinterpret_cast<SceneCommon*>(dynCBData[0]);

    pCommonCB->VP = camera.CalcViewMatrix() * camera.CalcProjMatrix(aspectRatioHdivW);
    pCommonCB->cameraPos = camera.CalcPos();
    pCommonCB->sceneParams.x = m_sceneParams.exposure;
    pCommonCB->intSceneParams.x = m_sceneParams.renderMode;

    pCommonCB->intSceneParams.y = 0;
    pCommonCB->intSceneParams.y |= (m_sceneParams.applyBloom ? RENDER_FLAG_APPLY_BLOOM : 0);
    pCommonCB->intSceneParams.y |= (m_sceneParams.pcf ? RENDER_FLAG_PCF_ON : 0);
    pCommonCB->intSceneParams.y |= (m_sceneParams.tintSplits ? RENDER_FLAG_TINT_SPLITS : 0);
    pCommonCB->intSceneParams.y |= (m_sceneParams.tintOutArea ? RENDER_FLAG_TINT_OUT_AREA : 0);

    pCommonCB->bloomRatio = m_sceneParams.bloomRatio;
    pCommonCB->useSpecAA = m_sceneParams.applySpecAA;

    pCommonCB->intSceneParams.z = m_sceneParams.shadowMode;

    pCommonCB->imageSize = Point4f{ (float)(rect.right - rect.left), (float)(rect.bottom - rect.top), 0, 0 };

    Point4f camPos = camera.CalcPos();
    Point3f right, up, dir;
    camera.CalcDirection(right, up, dir);
    pCommonCB->cameraWorldPosNear = Point4f{ camPos.x, camPos.y, camPos.z, camera.GetNear()};
    pCommonCB->cameraWorldDirFar = Point4f{ dir.x, dir.y, dir.z, camera.GetFar() };
    pCommonCB->shadowSplitDists.x = m_sceneParams.shadowSplitsDist[0];
    pCommonCB->shadowSplitDists.y = m_sceneParams.shadowSplitsDist[1];
    pCommonCB->shadowSplitDists.z = m_sceneParams.shadowSplitsDist[2];
    pCommonCB->shadowSplitDists.w = m_sceneParams.shadowSplitsDist[3];

    pCommonCB->localCubemapBasePosSize = Point4f( m_pCubemapBuilder->GetLocalParams().pos.x, 0, m_pCubemapBuilder->GetLocalParams().pos.y, m_pCubemapBuilder->GetLocalParams().size);
    pCommonCB->localCubemapGrid = Point4i(m_pCubemapBuilder->GetLocalParams().grid.x, m_pCubemapBuilder->GetLocalParams().grid.y, 0, 0);

    // Setup lights data
    SetupLights(reinterpret_cast<Lights*>(dynCBData[1]));

    // Setup common textures
    // -->
    if (m_pCubemapBuilder->GetCubemapCount() > 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.Format = m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).irradianceMap.pResource->GetDesc().Format;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        texDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).irradianceMap.pResource, &texDesc, beginParams.cpuTextureHandles);

        beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

        D3D12_SHADER_RESOURCE_VIEW_DESC brdfDesc = {};
        brdfDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        brdfDesc.Format = m_brdf.pResource->GetDesc().Format;
        brdfDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        brdfDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_brdf.pResource, &brdfDesc, beginParams.cpuTextureHandles);

        beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

        D3D12_SHADER_RESOURCE_VIEW_DESC envDesc = {};
        envDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        envDesc.Format = m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).envMap.pResource->GetDesc().Format;
        envDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        envDesc.Texture2D.MipLevels = CubemapBuilderParams.roughnessMips;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).envMap.pResource, &envDesc, beginParams.cpuTextureHandles);

        beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

        D3D12_SHADER_RESOURCE_VIEW_DESC cubeDesc = {};
        cubeDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        cubeDesc.Format = m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).cubemap.pResource->GetDesc().Format;
        cubeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        cubeDesc.Texture2D.MipLevels = 1;
        GetDevice()->GetDXDevice()->CreateShaderResourceView(m_pCubemapBuilder->GetCubemap(m_sceneParams.cubemapIdx).cubemap.pResource, &cubeDesc, beginParams.cpuTextureHandles);

        beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

        if (m_sceneParams.shadowMode == SceneParameters::ShadowModeSimple)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC shadowMapDesc = {};
            shadowMapDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shadowMapDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            shadowMapDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            shadowMapDesc.Texture2DArray.MostDetailedMip = 0;
            shadowMapDesc.Texture2DArray.MipLevels = 1;
            shadowMapDesc.Texture2DArray.ArraySize = 1;
            shadowMapDesc.Texture2DArray.FirstArraySlice = 0;
            GetDevice()->GetDXDevice()->CreateShaderResourceView(m_shadowMap.pResource, &shadowMapDesc, beginParams.cpuTextureHandles);
        }
        else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC shadowMapDesc = {};
            shadowMapDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shadowMapDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            shadowMapDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            shadowMapDesc.Texture2DArray.MostDetailedMip = 0;
            shadowMapDesc.Texture2DArray.MipLevels = 1;
            shadowMapDesc.Texture2DArray.ArraySize = ShadowSplits;
            shadowMapDesc.Texture2DArray.FirstArraySlice = 0;
            GetDevice()->GetDXDevice()->CreateShaderResourceView(m_shadowMapSplits.pResource, &shadowMapDesc, beginParams.cpuTextureHandles);
        }

        beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

        if (!m_pCubemapBuilder->HasLocalCubemapsToBuild() && m_pCubemapBuilder->GetLocalCubemapIrradiance().pResource != nullptr)
        {
            if (UseLocalCubemaps)
            {
                Platform::GPUResource localIrradiance = m_pCubemapBuilder->GetLocalCubemapIrradiance();

                D3D12_SHADER_RESOURCE_VIEW_DESC localIrradianceDesc = {};
                localIrradianceDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                localIrradianceDesc.Format = localIrradiance.pResource->GetDesc().Format;
                localIrradianceDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                localIrradianceDesc.Texture2DArray.MostDetailedMip = 0;
                localIrradianceDesc.Texture2DArray.MipLevels = 1;
                localIrradianceDesc.Texture2DArray.ArraySize = m_pCubemapBuilder->GetLocalParams().grid.x * m_pCubemapBuilder->GetLocalParams().grid.y;
                localIrradianceDesc.Texture2DArray.FirstArraySlice = 0;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(localIrradiance.pResource, &localIrradianceDesc, beginParams.cpuTextureHandles);

                beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

                Platform::GPUResource localEnvironment = m_pCubemapBuilder->GetLocalCubemapEnvironment();

                D3D12_SHADER_RESOURCE_VIEW_DESC localEnvironmentDesc = {};
                localEnvironmentDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                localEnvironmentDesc.Format = localEnvironment.pResource->GetDesc().Format;
                localEnvironmentDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                localEnvironmentDesc.Texture2DArray.MostDetailedMip = 0;
                localEnvironmentDesc.Texture2DArray.MipLevels = m_pCubemapBuilder->GetLocalParams().roughnessMips;
                localEnvironmentDesc.Texture2DArray.ArraySize = m_pCubemapBuilder->GetLocalParams().grid.x * m_pCubemapBuilder->GetLocalParams().grid.y;
                localEnvironmentDesc.Texture2DArray.FirstArraySlice = 0;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(localEnvironment.pResource, &localEnvironmentDesc, beginParams.cpuTextureHandles);
            }
        }
    }
    // <--
}

bool Renderer::CreateCubemapTests()
{
    int count = m_pCubemapBuilder->GetLocalParams().grid.x * m_pCubemapBuilder->GetLocalParams().grid.y;

    bool res = BeginGeometryCreation();
    for (int i = 0; i < count && res; i++)
    {
        TestGeometry geometry;
        CreateGeometryParams params;

        size_t vertexCount = 0;
        size_t indexCount = 0;
        std::vector<TextureVertex> cubeVertices;
        std::vector<UINT16> indices;
        Platform::GetCubeDataSize(false, true, indexCount, vertexCount);
        cubeVertices.resize(vertexCount);
        indices.resize(indexCount);
        Platform::CreateCube(&indices[0], sizeof(TextureVertex), &cubeVertices[0].pos, nullptr, &cubeVertices[0].uv, &cubeVertices[0].normal);

        params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
        params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 20 });
        params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
        params.indexFormat = DXGI_FORMAT_R16_UINT;
        params.pIndices = indices.data();
        params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        params.pShaderSourceName = _T("CubemapTest.hlsl");
        params.pVertices = cubeVertices.data();
        params.vertexDataSize = (UINT)cubeVertices.size() * sizeof(TextureVertex);
        params.vertexDataStride = sizeof(TextureVertex);
        params.rtFormat = HDRFormat;
        params.geomStaticTexturesCount = 0;

        res = CreateGeometry(params, geometry);
        if (res)
        {
            Point2f basePos = m_pCubemapBuilder->GetLocalParams().pos;

            Point2i posXY{
                i % m_pCubemapBuilder->GetLocalParams().grid.x,
                i / m_pCubemapBuilder->GetLocalParams().grid.x
            };
            Point3f testPos{
                posXY.x * m_pCubemapBuilder->GetLocalParams().size + m_pCubemapBuilder->GetLocalParams().size / 2 + basePos.x,
                m_pCubemapBuilder->GetLocalParams().size / 2,
                posXY.y * m_pCubemapBuilder->GetLocalParams().size + m_pCubemapBuilder->GetLocalParams().size / 2 + basePos.y
            };

            geometry.objData.transform = geometry.objData.transform * Matrix4f().Offset(testPos);
            geometry.objData.cubeParams = Point4f{
                testPos, (float)i
            };

            m_cubemapTestGeometries.push_back(geometry);
        }
    }

    EndGeometryCreation();

    return res;
}
