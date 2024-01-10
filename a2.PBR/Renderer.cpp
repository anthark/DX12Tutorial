#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformTexture.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

const int MetalnessSamples = 9;
const int RoughnessSamples = 9;
const float BaseLow = 0.01f;
const bool SingleCubemapPrepare = true;
const int RoughnessMips = 5;

const std::vector<const char*> SceneGeometryTypeNames = {"Single object", "Objects grid"};
const std::vector<const char*> RenderModeNames = { "Lighting", "  Diffuse", "  IBL Diffuse", "  Specular", "    Normal Distribution", "    Geometry", "    Fresnel", "  IBL Environment", "  IBL Fresnel", "  IBL BRDF" };

}

SceneParameters::SceneParameters()
    : exposure(1.0f)
    , showGrid(true)
    , showCubemap(true)
    , geomType(SceneGeometryTypeSingleObject)
    , renderMode(RenderModeLighting)
    , roughness(0.5f)
    , metalness(0.0)
    , dielectricF0(0.04f)
    , cubemapIdx(0)
{
    metalF0Srgb[0] = 1.00f;
    metalF0Srgb[1] = 0.71f;
    metalF0Srgb[2] = 0.29f;

    for (int i = 0; i < 3; i++)
    {
        metalF0Linear[i] = powf(metalF0Srgb[i], 2.2f);
    }

    showMenu = true;
}

const Point4f Renderer::BackColor = Point4f{0.25f,0.25f,0.25f,1};
const DXGI_FORMAT Renderer::HDRFormat = DXGI_FORMAT_R11G11B10_FLOAT;

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 2, 7, { sizeof(SceneCommon), sizeof(Lights) })
    , CameraControlEuler()
    , m_fpsCount(0)
    , m_prevFPS(0.0)
    , m_fps(0.0)
    , m_pLuminancePSO(nullptr)
    , m_pLuminanceRS(nullptr)
    , m_pLuminanceFinalPSO(nullptr)
    , m_pLuminanceFinalRS(nullptr)
    , m_lastUpdateDelta(0)
    , m_brightLightBrightness(100.0f)
    , m_value(0)
    , m_pTextDraw(nullptr)
{
    m_color[0] = m_color[1] = m_color[2] = 1.0f;
    m_textureForDelete.pResource = nullptr;
}

Renderer::~Renderer()
{
    assert(m_pTextDraw == nullptr);
    assert(m_pLuminancePSO == nullptr);
    assert(m_pLuminanceRS == nullptr);
    assert(m_pLuminanceFinalPSO == nullptr);
    assert(m_pLuminanceFinalRS == nullptr);
}

bool Renderer::Init(HWND hWnd)
{
    m_firstFrame = true;

    m_cubemapNamesToBeRendered = Platform::ScanFiles(_T("../Common"), _T("*.hdr"));

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

            Geometry geometry;
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

            for (int i = 0; i < MetalnessSamples && res; i++)
            {
                for (int j = 0; j < RoughnessSamples && res; j++)
                {
                    if (i == 0 && j == 0)
                    {
                        res = CreateGeometry(params, geometry);
                    }
                    else
                    {
                        res = CreateGeometrySharedState(m_geometries[0], params, geometry);
                    }
                    if (res)
                    {
                        m_geometries.push_back(geometry);
                    }
                }
            }
            // For single geometry
            if (res)
            {
                res = CreateGeometrySharedState(m_geometries[0], params, geometry);
                if (res)
                {
                    m_geometries.push_back(geometry);
                }
            }

            if (res)
            {
                float metalnessDelta = (1.0f - BaseLow) / (MetalnessSamples - 1);
                float roughnessDelta = (1.0f - BaseLow) / (RoughnessSamples - 1);

                Matrix4f trans;
                Matrix4f scale;
                scale.Scale(0.8f, 0.8f, 0.8f);
                for (int i = 0; i < MetalnessSamples; i++)
                {
                    for (int j = 0; j < RoughnessSamples; j++)
                    {
                        trans.Offset(Point3f{ (float)j - (float)(RoughnessSamples - 1) / 2, (float)i - (float)(MetalnessSamples - 1) / 2, 0 });

                        m_geometries[i*RoughnessSamples + j].objData.transform = scale * trans;
                        m_geometries[i*RoughnessSamples + j].objData.transformNormals = trans.Inverse().Transpose();
                        m_geometries[i*RoughnessSamples + j].objData.metalF0 = Point4f{ m_sceneParams.metalF0Linear[0], m_sceneParams.metalF0Linear[1], m_sceneParams.metalF0Linear[2], 1 };
                        m_geometries[i*RoughnessSamples + j].objData.pbr.x = BaseLow + (float)j * roughnessDelta;
                        m_geometries[i*RoughnessSamples + j].objData.pbr.y = BaseLow + (float)i * metalnessDelta;
                        m_geometries[i*RoughnessSamples + j].objData.pbr.z = m_sceneParams.dielectricF0;
                    }
                }

                m_geometries[RoughnessSamples*MetalnessSamples].objData.transform.Identity();
                m_geometries[RoughnessSamples*MetalnessSamples].objData.transformNormals.Identity();
                m_geometries[RoughnessSamples*MetalnessSamples].objData.metalF0 = Point4f{ m_sceneParams.metalF0Linear[0], m_sceneParams.metalF0Linear[1], m_sceneParams.metalF0Linear[2], 1 };
                m_geometries[RoughnessSamples*MetalnessSamples].objData.pbr.x = 0;
                m_geometries[RoughnessSamples*MetalnessSamples].objData.pbr.y = 0;
                m_geometries[RoughnessSamples*MetalnessSamples].objData.pbr.z = m_sceneParams.dielectricF0;
            }

            if (res)
            {
                params.pShaderSourceName = _T("Cubemap.hlsl");
                params.geomStaticTexturesCount = 0;

                params.geomStaticTextures.clear();
                //params.geomStaticTextures.push_back({ m_cubeMaps[0].pResource , D3D12_SRV_DIMENSION_TEXTURECUBE});
                //params.geomStaticTextures.push_back({ m_cubeMapsConvoluted[0].pResource , D3D12_SRV_DIMENSION_TEXTURECUBE });
                //params.geomStaticTextures.push_back({ m_envCubemaps[0].pResource , D3D12_SRV_DIMENSION_TEXTURECUBE });
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
                geometry = Geometry();
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

            EndGeometryCreation();
        }

        if (res)
        {
            res = GetDevice()->AllocateRenderTargetView(m_hdrRTV);
            if (res)
            {
                res = GetDevice()->AllocateStaticDescriptors(1, m_hdrSRVCpu, m_hdrSRV);
            }
            if (res)
            {
                res = CreateHDRTexture();
            }
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
            res = GetDevice()->AllocateRenderTargetView(m_cubeMapRTV);
            if (res)
            {
                res = CreateCubeMapRT();
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
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("IrradianceConvolution.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = TRUE;
            geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

            res = CreateGeometryState(geomStateParams, m_irradianceConvolutionState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("EnvironmentConvolution.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = TRUE;
            geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

            res = CreateGeometryState(geomStateParams, m_environmentConvolutionState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("SimpleCopy.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
            geomStateParams.dsFormat = DXGI_FORMAT_UNKNOWN;
            geomStateParams.depthStencilState.DepthEnable = FALSE;

            res = CreateGeometryState(geomStateParams, m_simpleCopyState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("EquirectToCubemapFace.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = TRUE;
            geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

            res = CreateGeometryState(geomStateParams, m_equirectToCubemapFaceState);
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 8 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Tonemap.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = FALSE;

            res = CreateGeometryState(geomStateParams, m_tonemapGeomState);
        }

        if (res)
        {
            res = CreateComputePipeline();
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
    GetDevice()->WaitGPUIdle();

    TERM_RELEASE(m_pTextDraw);

    ImGui_ImplWin32_Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui::DestroyContext();

    DestroyComputePipeline();

    for (auto geometry : m_geometries)
    {
        DestroyGeometry(geometry);
    }
    m_geometries.clear();

    for (auto geometry : m_serviceGeometries)
    {
        DestroyGeometry(geometry);
    }
    m_serviceGeometries.clear();

    DestroyGeometryState(m_tonemapGeomState);
    DestroyGeometryState(m_simpleCopyState);
    DestroyGeometryState(m_environmentConvolutionState);
    DestroyGeometryState(m_irradianceConvolutionState);
    DestroyGeometryState(m_equirectToCubemapFaceState);
    DestroyGeometryState(m_integrateBRDFState);

    DestroyCubeMapRT();
    DestroyBRDFTexture();
    DestroyHDRTexture();

    if (SingleCubemapPrepare)
    {
        //assert(m_cubemapsToBeRendered.empty());
    }
    else
    {
        ClearCubemapIntermediates();
    }

    for (auto& res : m_cubeMaps)
    {
        GetDevice()->ReleaseGPUResource(res);
    }
    m_cubeMaps.clear();

    for (auto& res : m_irradianceCubemaps)
    {
        GetDevice()->ReleaseGPUResource(res);
    }
    m_irradianceCubemaps.clear();

    for (auto& res : m_envCubemaps)
    {
        GetDevice()->ReleaseGPUResource(res);
    }
    m_envCubemaps.clear();

    Platform::BaseRenderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 4 * deltaSec;

    m_lastUpdateDelta = (float)(deltaSec);

    UpdateCamera(deltaSec);

    for (int i = 0; i < MetalnessSamples; i++)
    {
        for (int j = 0; j < RoughnessSamples; j++)
        {
            m_geometries[i*RoughnessSamples + j].objData.metalF0 = Point4f{ m_sceneParams.metalF0Linear[0], m_sceneParams.metalF0Linear[1], m_sceneParams.metalF0Linear[2], 1 };
            m_geometries[i*RoughnessSamples + j].objData.pbr.z = m_sceneParams.dielectricF0;
        }
    }
    m_geometries[RoughnessSamples * MetalnessSamples].objData.pbr.x = m_sceneParams.roughness;
    m_geometries[RoughnessSamples * MetalnessSamples].objData.pbr.y = m_sceneParams.metalness;
    m_geometries[RoughnessSamples * MetalnessSamples].objData.metalF0 = Point4f{ m_sceneParams.metalF0Linear[0], m_sceneParams.metalF0Linear[1], m_sceneParams.metalF0Linear[2], 1 };
    m_geometries[RoughnessSamples * MetalnessSamples].objData.pbr.z = m_sceneParams.dielectricF0;

    Point4f cameraPos = m_camera.CalcPos();
    Matrix4f trans;
    Matrix4f scale;
    scale.Scale(2.0f, 2.0f, 2.0f);
    trans.Offset(Point3f{cameraPos.x, cameraPos.y, cameraPos.z});
    m_serviceGeometries[0].objData.transform = scale * trans;
    m_serviceGeometries[0].objData.transformNormals = trans.Inverse().Transpose();

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
    if (BeginRender(beginParams))
    {
        if (IsCreationFrame())
        {
            IntegrateBRDF();

            m_firstFrame = false;
        }
        else
        {
            // Setup VP matrix
            SceneCommon* pCommonCB = reinterpret_cast<SceneCommon*>(dynCBData[0]);

            pCommonCB->VP = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);
            pCommonCB->cameraPos = GetCamera()->CalcPos();
            pCommonCB->sceneParams.x = m_sceneParams.exposure;
            pCommonCB->intSceneParams.x = m_sceneParams.renderMode;

            // Setup lights data
            SetupLights(reinterpret_cast<Lights*>(dynCBData[1]));

            // Setup common textures
            // -->
            if (!m_irradianceCubemaps.empty())
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_irradianceCubemaps[m_sceneParams.cubemapIdx].pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_irradianceCubemaps[m_sceneParams.cubemapIdx].pResource, &texDesc, beginParams.cpuTextureHandles);

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
                envDesc.Format = m_envCubemaps[m_sceneParams.cubemapIdx].pResource->GetDesc().Format;
                envDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                envDesc.Texture2D.MipLevels = RoughnessMips;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_envCubemaps[m_sceneParams.cubemapIdx].pResource, &envDesc, beginParams.cpuTextureHandles);

                beginParams.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize();

                D3D12_SHADER_RESOURCE_VIEW_DESC cubeDesc = {};
                cubeDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                cubeDesc.Format = m_cubeMaps[m_sceneParams.cubemapIdx].pResource->GetDesc().Format;
                cubeDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                cubeDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_cubeMaps[m_sceneParams.cubemapIdx].pResource, &cubeDesc, beginParams.cpuTextureHandles);
            }
            // <--

            if (HasCubemapsForBuild(cubemapName))
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Building cubemap: %ls"), cubemapName.c_str());

                RenderCubemaps();
            }
            else if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
            {
                if (m_textureForDelete.pResource != nullptr)
                {
                    GetDevice()->WaitGPUIdle();
                    GetDevice()->ReleaseGPUResource(m_textureForDelete);
                    m_textureForDelete.pResource = nullptr;

                    // Copy loaded cubemap names for menu selection array
                    for (const auto& str : m_loadedCubemaps)
                    {
                        m_menuCubemaps.push_back(str.c_str());
                    }
                }

                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDSVHandle();
                GetCurrentCommandList()->OMSetRenderTargets(1, &m_hdrRTV, TRUE, &dsvHandle);

                GetCurrentCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

                D3D12_VIEWPORT viewport = GetViewport();
                GetCurrentCommandList()->RSSetViewports(1, &viewport);
                GetCurrentCommandList()->RSSetScissorRects(1, &rect);

                FLOAT clearColor[4] = { BackColor.x, BackColor.y, BackColor.z, BackColor.w };
                GetCurrentCommandList()->ClearRenderTargetView(m_hdrRTV, clearColor, 1, &rect);

                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                if (m_sceneParams.geomType == SceneParameters::SceneGeometryTypeObjectsGrid)
                {
                    for (int i = 0; i < RoughnessSamples * MetalnessSamples; i++)
                    {
                        RenderGeometry(m_geometries[i]);
                    }
                }
                else
                {
                    RenderGeometry(m_geometries[RoughnessSamples * MetalnessSamples]);
                }
                if (m_sceneParams.showCubemap)
                {
                    RenderGeometry(m_serviceGeometries[0]);
                }

                GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                SetBackBufferRT(); // Return current back buffer as render target

                MeasureLuminance();

                Tonemap();

                if (m_sceneParams.showGrid)
                {
                    RenderGeometry(m_serviceGeometries[1]);
                }

                if (m_sceneParams.showMenu)
                {
                    // render your GUI
                    ImGui::Begin("Scene parameters");
                    ImGui::Text("Scene");
                    ImGui::SliderFloat("Exposure", &m_sceneParams.exposure, 0.001f, 10.0f);
                    ImGui::Checkbox("Show grid", &m_sceneParams.showGrid);
                    ImGui::Checkbox("Show cubemap", &m_sceneParams.showCubemap);
                    ImGui::ListBox("Geometry type", (int*)&m_sceneParams.geomType, SceneGeometryTypeNames.data(), (int)SceneGeometryTypeNames.size());
                    ImGui::ListBox("Render mode", (int*)&m_sceneParams.renderMode, RenderModeNames.data(), (int)RenderModeNames.size());
                    ImGui::ListBox("Cubemap", &m_sceneParams.cubemapIdx, m_menuCubemaps.data(), (int)m_menuCubemaps.size());
                    ImGui::Text("Object");
                    if (m_sceneParams.geomType == SceneParameters::SceneGeometryTypeSingleObject)
                    {
                        ImGui::SliderFloat("Roughness", &m_sceneParams.roughness, 0.0f, 1.0f);
                        ImGui::SliderFloat("Metalness", &m_sceneParams.metalness, 0.0f, 1.0f);
                    }
                    ImGui::SliderFloat("Dielectric F0", &m_sceneParams.dielectricF0, 0.0f, 1.0f);
                    ImGui::ColorEdit3("Metal F0", m_sceneParams.metalF0Srgb);
                    ImGui::End();
                }

                // Render dear imgui into screen
                ImGui::Render();
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandList());

                for (int i = 0; i < 3; i++)
                {
                    m_sceneParams.metalF0Linear[i] = powf(m_sceneParams.metalF0Srgb[i], 2.2f);
                }

                // Reset state to ours after ImGui draw
                ResetRender();

                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("FPS: %5.2f"), m_fps);
            }
        }

        EndRender();

        if (SingleCubemapPrepare)
        {
            ClearCubemapIntermediates();
        }

        return true;
    }

    return false;
}

bool Renderer::OnKeyDown(int virtualKeyCode)
{
    switch (virtualKeyCode)
    {
        case 'x':
        case 'X':
            if (m_brightLightBrightness == 1.0f)
            {
                m_brightLightBrightness = 10.0f;
            }
            else if (m_brightLightBrightness == 10.0f)
            {
                m_brightLightBrightness = 100.0f;
            }
            else
            {
                m_brightLightBrightness = 1.0f;
            }
            return true;
            break;

        case 192: // Tilda
            {
                m_sceneParams.showMenu = !m_sceneParams.showMenu;
            }
            break;
    }

    return CameraControlEuler::OnKeyDown(virtualKeyCode);
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

void Renderer::Tonemap()
{
    UINT64 gpuVirtualAddress;

    PostprocessingVertex* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(4 * sizeof(PostprocessingVertex));
    bool res = GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        // Fill
        pVertices[0].pos = Point2f{ -1, -1 };
        pVertices[1].pos = Point2f{  1, -1 };
        pVertices[2].pos = Point2f{  1,  1 };
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
        SetupGeometryState(m_tonemapGeomState);

        GetCurrentCommandList()->SetGraphicsRootConstantBufferView(1, m_tonemapParams.pResource->GetGPUVirtualAddress());

        GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, m_hdrSRV);

        GetCurrentCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
}

void Renderer::SetupLights(Lights* pLights)
{
    pLights->ambientColor = Point3f{ 0.1f, 0.1f, 0.1f };

    unsigned int idx = 0;

    static const float delta = 0.75f;

    // Red point light 0
    {
        pLights->lights[idx].type = LT_Point;
        pLights->lights[idx].pos = Point3f{ 0.0, 0.0f, -5.0f };
        pLights->lights[idx].color = Point4f{ m_color[0], m_color[1], m_color[2], 0 } * 2.0f * m_brightLightBrightness;

        ++idx;
    }

#if 0
    // Red point light 1
    {
        pLights->lights[idx].type = LT_Point;
        pLights->lights[idx].pos = Point3f{ -1.5f - delta, 0.5f, delta };
        pLights->lights[idx].color = Point4f{ 1.0f, 0, 0, 0 };

        ++idx;
    }

    // Red point light 2
    {
        pLights->lights[idx].type = LT_Point;
        pLights->lights[idx].pos = Point3f{ -1.5f - delta, 0.5f, -delta };
        pLights->lights[idx].color = Point4f{ 1.0f, 0, 0, 0 };

        ++idx;
    }
#endif

    pLights->lightCount = idx;
}

bool Renderer::CreateHDRTexture()
{
    assert(m_hdrRT.pResource == nullptr);

    UINT height = GetRect().bottom - GetRect().top;
    UINT width = GetRect().right - GetRect().left;

    Platform::CreateTextureParams params;
    //params.format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    return res;
}

void Renderer::DestroyHDRTexture()
{
    GetDevice()->ReleaseGPUResource(m_lumTexture1);
    GetDevice()->ReleaseGPUResource(m_lumTexture0);
    GetDevice()->ReleaseGPUResource(m_hdrRT);
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

    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(sizeof(TonemapParams), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, m_tonemapParams);
    }

    return res;
}

void Renderer::DestroyComputePipeline()
{
    GetDevice()->ReleaseGPUResource(m_tonemapParams);

    D3D_RELEASE(m_pLuminanceFinalPSO);
    D3D_RELEASE(m_pLuminanceFinalRS);
    D3D_RELEASE(m_pLuminancePSO);
    D3D_RELEASE(m_pLuminanceRS);
}

bool Renderer::LoadHDRTexture(LPCTSTR filename, Platform::GPUResource* pResource)
{
    FILE* pFile = nullptr;
    errno_t error = _tfopen_s(&pFile, filename, _T("rb"));
    assert(error == 0 && pFile != nullptr);
    if (error != 0 || pFile == nullptr)
    {
        return false;
    }

    int width, height;
    int components;
    float* pHDRData = stbi_loadf_from_file(pFile, &width, &height, &components, 3);
    assert(components == 3);

    bool res = components == 3 && pHDRData != nullptr;
    if (res)
    {
        float* pHDR4Data = (float*)malloc(width * height * 4 * sizeof(float));
        memset(pHDR4Data, 0, width * height * 4 * sizeof(float));
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                float* pSrcData = pHDRData + (i*width + j) * 3;
                float* pDstData = pHDR4Data + (i*width + j) * 4;

                for (int k = 0; k < 3; k++)
                {
                    pDstData[k] = pSrcData[k];
                }
            }
        }

        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = height;
        params.width = width;
        params.initialState = D3D12_RESOURCE_STATE_COMMON;

        res = Platform::CreateTexture(params, false, GetDevice(), *pResource, pHDR4Data, width * height * 4 * sizeof(float));

        free(pHDR4Data);
    }

    if (pHDRData != nullptr)
    {
        free(pHDRData);
    }
    fclose(pFile);

    return res;
}

bool Renderer::CreateCubeMapRT()
{
    assert(m_cubeMapRT.pResource == nullptr);

    Platform::CreateTextureParams params;
    params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    params.height = CubemapRes;
    params.width = CubemapRes;
    params.enableRT = true;
    params.initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = params.format;
    memset(&clearValue.Color, 0, sizeof(clearValue.Color));
    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, GetDevice(), m_cubeMapRT);

    if (res)
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = params.format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;

        GetDevice()->GetDXDevice()->CreateRenderTargetView(m_cubeMapRT.pResource, &desc, m_cubeMapRTV);
    }

    return res;
}

void Renderer::DestroyCubeMapRT()
{
    GetDevice()->ReleaseGPUResource(m_cubeMapRT);
}

bool Renderer::RenderCubemaps()
{
    if (m_textureForDelete.pResource != nullptr)
    {
        GetDevice()->WaitGPUIdle();
        GetDevice()->ReleaseGPUResource(m_textureForDelete);
        m_textureForDelete.pResource = nullptr;
    }

    if (m_cubemapNamesToBeRendered.empty())
    {
        return true;
    }

    Platform::GPUResource equirectCubemap = {};
    bool res = BeginGeometryCreation();
    if (res)
    {
        res = LoadHDRTexture(m_cubemapNamesToBeRendered.front().c_str(), &equirectCubemap);

        EndGeometryCreation();
    }

    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = CubemapRes;
        params.width = CubemapRes;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        params.arraySize = 6; // For cubemap
        params.mips = CubemapMips;

        Platform::CreateTextureParams paramsIrradiance;
        paramsIrradiance.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        paramsIrradiance.height = IrradianceRes;
        paramsIrradiance.width = IrradianceRes;
        paramsIrradiance.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        paramsIrradiance.arraySize = 6; // For irradiance cubemap

        Platform::CreateTextureParams paramsEnvironment;
        paramsEnvironment.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        paramsEnvironment.height = EnvRes;
        paramsEnvironment.width = EnvRes;
        paramsEnvironment.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        paramsEnvironment.arraySize = 6; // For environment cubemap
        paramsEnvironment.mips = RoughnessMips;

        Platform::GPUResource cubeMap;
        res = Platform::CreateTexture(params, false, GetDevice(), cubeMap);
        if (res)
        {
            m_cubeMaps.push_back(cubeMap);
        }

        if (res)
        {
            res = Platform::CreateTexture(paramsIrradiance, false, GetDevice(), cubeMap);
        }
        if (res)
        {
            m_irradianceCubemaps.push_back(cubeMap);
        }

        if (res)
        {
            res = Platform::CreateTexture(paramsEnvironment, false, GetDevice(), cubeMap);
        }
        if (res)
        {
            m_envCubemaps.push_back(cubeMap);
        }
    }

    if (res)
    {
        assert(IrradianceRes <= CubemapRes);
        assert(EnvRes <= CubemapRes);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        res = GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        if (res)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
            texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            texDesc.Format = equirectCubemap.pResource->GetDesc().Format;
            texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            texDesc.Texture2D.MipLevels = 1;
            GetDevice()->GetDXDevice()->CreateShaderResourceView(equirectCubemap.pResource, &texDesc, cpuHandle);

            res = RenderToCubemap(m_cubeMaps.back(), m_cubeMapRT, m_cubeMapRTV, m_equirectToCubemapFaceState, gpuHandle, CubemapRes, 0, CubemapMips);
            if (res)
            {
                res = BuildCubemapMips(m_cubeMaps.back());
            }
            if (res)
            {
                cpuHandle.ptr += GetDevice()->GetSRVDescSize();
                gpuHandle.ptr += GetDevice()->GetSRVDescSize();
            }

            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_cubeMaps.back().pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_cubeMaps.back().pResource, &texDesc, cpuHandle);

                res = RenderToCubemap(m_irradianceCubemaps.back(), m_cubeMapRT, m_cubeMapRTV, m_irradianceConvolutionState, gpuHandle, IrradianceRes, 0, 1);
            }
            if (res)
            {
                cpuHandle.ptr += GetDevice()->GetSRVDescSize();
                gpuHandle.ptr += GetDevice()->GetSRVDescSize();
            }

            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_cubeMaps.back().pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = CubemapMips;
                texDesc.Texture2D.MostDetailedMip = 0;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_cubeMaps.back().pResource, &texDesc, cpuHandle);

                for (int i = 0; i < RoughnessMips && res; i++)
                {
                    res = RenderToCubemap(m_envCubemaps.back(), m_cubeMapRT, m_cubeMapRTV, m_environmentConvolutionState, gpuHandle, EnvRes, i, RoughnessMips);
                }
            }
        }
    }

    if (res)
    {
        m_loadedCubemaps.push_back(ToString(StripExtension(ShortFilename(m_cubemapNamesToBeRendered.front()))));
    }

    if (SingleCubemapPrepare)
    {
        m_cubemapNamesToBeRendered.erase(m_cubemapNamesToBeRendered.begin());

        if (equirectCubemap.pResource != nullptr)
        {
            m_textureForDelete = equirectCubemap;
        }
    }

    return res;
}

bool Renderer::HasCubemapsForBuild(std::wstring& name)
{
    if (!m_cubemapNamesToBeRendered.empty())
    {
        name = m_cubemapNamesToBeRendered.front();
    }
    return !m_cubemapNamesToBeRendered.empty();
}

void Renderer::ClearCubemapIntermediates()
{
    // TODO
    /*if (!m_cubemapsToBeRendered.empty())
    {
        for (auto cubemap : m_cubemapsToBeRendered)
        {
            GetDevice()->ReleaseGPUResource(cubemap);
        }
        m_cubemapsToBeRendered.clear();
    }*/
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

bool Renderer::BuildCubemapMips(const Platform::GPUResource& resource)
{
    GetCurrentCommandList()->OMSetRenderTargets(1, &m_cubeMapRTV, TRUE, NULL);

    UINT pixels = CubemapRes;

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

    SetupGeometryState(m_simpleCopyState);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;

    res = GetDevice()->AllocateDynamicDescriptors(6 * (CubemapMips - 1), cpuHandle, gpuHandle);

    for (int j = 1; j < CubemapMips && res; j++)
    {
        pixels /= 2;

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (FLOAT)pixels;
        viewport.Height = (FLOAT)pixels;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        GetCurrentCommandList()->RSSetViewports(1, &viewport);

        D3D12_RECT rect = {};
        rect.bottom = pixels;
        rect.right = pixels;
        GetCurrentCommandList()->RSSetScissorRects(1, &rect);

        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.Format = resource.pResource->GetDesc().Format;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        texDesc.Texture2DArray.MipLevels = 1;
        texDesc.Texture2DArray.MostDetailedMip = j - 1;

        for (int i = 0; i < 6 && res; i++)
        {
            texDesc.Texture2DArray.ArraySize = 1;
            texDesc.Texture2DArray.FirstArraySlice = (UINT)i;
            GetDevice()->GetDXDevice()->CreateShaderResourceView(resource.pResource, &texDesc, cpuHandle);

            GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, gpuHandle);

            GetCurrentCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);

            res = GetDevice()->TransitResourceState(GetCurrentCommandList(), resource.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, (UINT)j + i * CubemapMips);
            if (res)
            {
                res = GetDevice()->TransitResourceState(GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                if (res)
                {
                    CD3DX12_TEXTURE_COPY_LOCATION dstLoc{ resource.pResource, (UINT)j + i * CubemapMips };
                    CD3DX12_TEXTURE_COPY_LOCATION srcLoc{ m_cubeMapRT.pResource };

                    D3D12_BOX rect = {};
                    rect.front = 0;
                    rect.back = 1;
                    rect.right = rect.bottom = pixels;

                    GetCurrentCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &rect);

                    GetDevice()->TransitResourceState(GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }

                GetDevice()->TransitResourceState(GetCurrentCommandList(), resource.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (UINT)j + i * CubemapMips);
            }

            cpuHandle.ptr += GetDevice()->GetSRVDescSize();
            gpuHandle.ptr += GetDevice()->GetSRVDescSize();
        }
    }

    return res;
}
