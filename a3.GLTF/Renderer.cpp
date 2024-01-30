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
const std::vector<const char*> RenderModeNames = { "Lighting", "  Diffuse", "  IBL Diffuse", "  Specular", "    Normal Distribution", "    Geometry", "    Fresnel", "  IBL Environment", "  IBL Fresnel", "  IBL BRDF", "Normals" };
const std::vector<const char*> BlurModeNames = { "Naive", "Separated", "Compute"};

}

SceneParameters::SceneParameters()
    : exposure(10.0f)
    , bloomThreshold(1.0f)
    , showGrid(false)
    , showCubemap(true)
    , applyBloom(false)
    , renderMode(RenderModeLighting)
    , blurMode(Naive)
    , cubemapIdx(0)
{
    showMenu = true;

    activeLightCount = 1;
    lights[0].color = Point3f{ 1,1,1 };
    lights[0].intensity = 100.0f;
    lights[0].distance = 5.0f;
    lights[0].inverseDirSphere = Point2f{(float)(1.5 * M_PI), 0.0f};
}

const Point4f Renderer::BackColor = Point4f{0.25f,0.25f,0.25f,1};
const DXGI_FORMAT Renderer::HDRFormat = DXGI_FORMAT_R11G11B10_FLOAT;

const Platform::CubemapBuilder::InitParams& Renderer::CubemapBuilderParams = {
    512,    // Cubemap resolution
    9,      // Cubemap mips
    32,     // Irradiance map resolution
    128,    // Environment map resolution
    5       // Roughness mips
};

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 2, 32, { sizeof(SceneCommon), sizeof(Lights) })
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
    , m_pModel(nullptr)
    , m_pCubemapBuilder(nullptr)
    , m_animationTime(0.0f)
{
    m_color[0] = m_color[1] = m_color[2] = 1.0f;

    // Models for loading should be placed in a folder named "Common/Models" in solution folder

    m_modelsToLoad.push(L"Porsche930");
    //m_modelsToLoad.push(L"LightSaber");
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
            res = GetDevice()->AllocateRenderTargetView(m_hdrRTV, 2);
            if (res)
            {
                res = GetDevice()->AllocateStaticDescriptors(1, m_hdrSRVCpu, m_hdrSRV);
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
            geomStateParams.geomStaticTexturesCount = 1;
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

    TERM_RELEASE(m_pCubemapBuilder);

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

    for (auto geometry : m_blendGeometries)
    {
        DestroyGeometry(geometry);
    }
    m_blendGeometries.clear();

    for (auto geometry : m_serviceGeometries)
    {
        DestroyGeometry(geometry);
    }
    m_serviceGeometries.clear();

    DestroyGeometryState(m_gaussBlurHorizontal);
    DestroyGeometryState(m_gaussBlurVertical);
    DestroyGeometryState(m_gaussBlurNaive);
    DestroyGeometryState(m_detectFlaresState);

    DestroyGeometryState(m_tonemapGeomState);
    DestroyGeometryState(m_integrateBRDFState);

    DestroyBRDFTexture();
    DestroyHDRTexture();

    for (auto& res : m_modelTextures)
    {
        GetDevice()->ReleaseGPUResource(res);
    }
    m_modelTextures.clear();

    Platform::BaseRenderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 4 * deltaSec;

    m_lastUpdateDelta = (float)(deltaSec);

    UpdateCamera(deltaSec);

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

    if (!m_nodes.empty()) // Assume model is loaded
    {
        // AAV TEMP
        //m_animationTime += (float)deltaSec;
        //ApplyAnimation(m_animationTime);

        UpdateMatrices();
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
            // Setup VP matrix
            SceneCommon* pCommonCB = reinterpret_cast<SceneCommon*>(dynCBData[0]);

            pCommonCB->VP = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);
            pCommonCB->cameraPos = GetCamera()->CalcPos();
            pCommonCB->sceneParams.x = m_sceneParams.exposure;
            pCommonCB->sceneParams.y = m_sceneParams.bloomThreshold;
            pCommonCB->intSceneParams.x = m_sceneParams.renderMode;
            pCommonCB->intSceneParams.y = m_sceneParams.applyBloom ? 1 : 0;
            pCommonCB->imageSize = Point4f{(float)(rect.right - rect.left), (float)(rect.bottom - rect.top), 0, 0};

            assert(m_nodeMatrices.size() <= MAX_NODES);
            memcpy(pCommonCB->nodeTransform, m_nodeMatrices.data(), m_nodeMatrices.size() * sizeof(Matrix4f));
            memcpy(pCommonCB->nodeNormalTransform, m_nodeNormalMatrices.data(), m_nodeNormalMatrices.size() * sizeof(Matrix4f));
            memcpy(pCommonCB->jointIndices, m_jointIndices.data(), m_jointIndices.size() * sizeof(int));

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
            }
            // <--

            if (HasModelToLoad(modelName))
            {
                m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("Loading model: %ls"), modelName.c_str());

                ProcessModelLoad();
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
            else if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
                && GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
            {
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
                for (int i = 0; i < m_geometries.size(); i++)
                {
                    RenderGeometry(m_geometries[i]);
                }
                if (m_sceneParams.showCubemap)
                {
                    RenderGeometry(m_serviceGeometries[0]);
                }
                // Transparent
                for (int i = 0; i < m_blendGeometries.size(); i++)
                {
                    RenderGeometry(m_blendGeometries[i]);
                }

                GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                SetBackBufferRT(); // Return current back buffer as render target

                MeasureLuminance();

                int finalBloomIdx = -1;
                {
                    GetCurrentCommandList()->OMSetRenderTargets(1, &m_bloomRTV[0], TRUE, nullptr);

                    DetectFlares();

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
                    ImGui::Text("Scene");
                    ImGui::SliderFloat("Exposure", &m_sceneParams.exposure, 0.001f, 10.0f);
                    ImGui::SliderFloat("BloomThreshold", &m_sceneParams.bloomThreshold, 0.5f, 10.0f);
                    ImGui::Checkbox("Show grid", &m_sceneParams.showGrid);
                    ImGui::Checkbox("Show cubemap", &m_sceneParams.showCubemap);
                    ImGui::Checkbox("Apply bloom", &m_sceneParams.applyBloom);
                    ImGui::SliderInt("Lights", &m_sceneParams.activeLightCount, 1, 4);
                    ImGui::ListBox("Render mode", (int*)&m_sceneParams.renderMode, RenderModeNames.data(), (int)RenderModeNames.size());
                    ImGui::ListBox("Blur mode", (int*)&m_sceneParams.blurMode, BlurModeNames.data(), (int)BlurModeNames.size());
                    ImGui::ListBox("Cubemap", &m_sceneParams.cubemapIdx, m_menuCubemaps.data(), (int)m_menuCubemaps.size());
                    ImGui::End();

                    for (int i = 0; i < m_sceneParams.activeLightCount; i++)
                    {
                        ImGui::Begin("Light 0");
                        ImGui::SliderFloat("Theta", &m_sceneParams.lights[i].inverseDirSphere.x, 0.0f, (float)(2.0 * M_PI));
                        ImGui::SliderFloat("Phi", &m_sceneParams.lights[i].inverseDirSphere.y, -(float)(0.5 * M_PI), (float)(0.5 * M_PI));
                        ImGui::SliderFloat("Distance", &m_sceneParams.lights[i].distance, 0.01f, 100.0f);
                        ImGui::SliderFloat("Intensity", &m_sceneParams.lights[i].intensity, 1.0f, 500.0f);
                        ImGui::ColorEdit3("Color", (float*)&m_sceneParams.lights[i].color);
                        ImGui::End();
                    }
                }

                // Render dear imgui into screen
                ImGui::Render();
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandList());

                // Reset state to ours after ImGui draw
                ResetRender();

                m_pTextDraw->DrawText(m_fontId, Point3f{1,1,1}, _T("FPS: %5.2f"), m_fps);
            }
        }

        EndRender();

        return true;
    }

    return false;
}

bool Renderer::OnKeyDown(int virtualKeyCode)
{
    switch (virtualKeyCode)
    {
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

bool Renderer::GaussBlur(int& finalBloomIdx)
{
    switch (m_sceneParams.blurMode)
    {
        case SceneParameters::Naive:
            {
                for (int i = 0; i < BlurSteps; i++)
                {
                    int targetIdx = (i + 1) % 2;

                    if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[targetIdx].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
                    {
                        GetCurrentCommandList()->OMSetRenderTargets(1, &m_bloomRTV[targetIdx], TRUE, nullptr);

                        CallPostProcess(m_gaussBlurNaive, m_bloomSRV[i % 2]);

                        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[targetIdx].pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
                }
                finalBloomIdx = (BlurSteps + 1) % 2;
            }
            break;

        case SceneParameters::Separated:
            {
                for (int i = 0; i < BlurSteps; i++)
                {
                    if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[1].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
                    {
                        GetCurrentCommandList()->OMSetRenderTargets(1, &m_bloomRTV[1], TRUE, nullptr);

                        CallPostProcess(m_gaussBlurVertical, m_bloomSRV[0]);

                        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[1].pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
                    if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
                    {
                        GetCurrentCommandList()->OMSetRenderTargets(1, &m_bloomRTV[0], TRUE, nullptr);

                        CallPostProcess(m_gaussBlurHorizontal, m_bloomSRV[1]);

                        GetDevice()->TransitResourceState(GetCurrentCommandList(), m_bloomRT[0].pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
                }
                finalBloomIdx = 0;
            }
            break;

        case SceneParameters::Compute:
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
            break;
    }

    return true;
}

void Renderer::SetupLights(Lights* pLights)
{
    pLights->ambientColor = Point3f{ 0.1f, 0.1f, 0.1f };

    for (int i = 0; i < m_sceneParams.activeLightCount; i++)
    {
        const auto& light = m_sceneParams.lights[i];

        pLights->lights[i].type = LT_Point;
        pLights->lights[i].pos = Point3f{
            cosf(light.inverseDirSphere.y) * cosf(light.inverseDirSphere.x) * light.distance,
            sinf(light.inverseDirSphere.y) * light.distance,
            cosf(light.inverseDirSphere.y) * sinf(light.inverseDirSphere.x) * light.distance
        };
        pLights->lights[i].color = Point4f{ light.color.x, light.color.y, light.color.z } * light.intensity;
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

bool Renderer::HasModelToLoad(std::wstring& name)
{
    if (!m_modelsToLoad.empty())
    {
        name = m_modelsToLoad.front();
    }
    return !m_modelsToLoad.empty();
}

bool Renderer::ProcessModelLoad()
{
    bool res = true;
    if (m_pModel == nullptr)
    {
        std::wstring name = m_modelsToLoad.front();

        res = LoadModel(name, &m_pModel);
        if (res)
        {
            m_modelSRGB.resize(m_pModel->images.size(), false);

            for (int i = 0; i < m_pModel->materials.size(); i++)
            {
            if (m_pModel->materials[i].pbrMetallicRoughness.baseColorTexture.index != -1)
            {
                int imageIdx = m_pModel->textures[m_pModel->materials[i].pbrMetallicRoughness.baseColorTexture.index].source;
                m_modelSRGB[imageIdx] = true;
            }
            }
        }
        // Setup skin transforms
        if (res && !m_pModel->skins.empty())
        {
            assert(m_pModel->skins.size() == 1);

            tinygltf::Accessor invBindMatricesAccessor = m_pModel->accessors[m_pModel->skins[0].inverseBindMatrices];

            assert(invBindMatricesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(invBindMatricesAccessor.type == TINYGLTF_TYPE_MAT4);

            tinygltf::BufferView invBindMatricesView = m_pModel->bufferViews[invBindMatricesAccessor.bufferView];
            tinygltf::Buffer invBindMatricesBuffer = m_pModel->buffers[invBindMatricesView.buffer];

            const Matrix4f* pInvBindMatrices = reinterpret_cast<const Matrix4f*>(invBindMatricesBuffer.data.data() + invBindMatricesView.byteOffset + invBindMatricesAccessor.byteOffset);

            m_jointIndices.resize(m_pModel->skins[0].joints.size());
            for (int i = 0; i < m_pModel->skins[0].joints.size(); i++)
            {
                m_jointIndices[i] = m_pModel->skins[0].joints[i];

                int nodeIdx = m_pModel->skins[0].joints[i];

                if (nodeIdx >= m_nodeInvBindMatrices.size())
                {
                    m_nodeInvBindMatrices.resize(nodeIdx + 1, Matrix4f());
                }
                m_nodeInvBindMatrices[nodeIdx] = pInvBindMatrices[i];
            }
        }
        // Setup animations
        if (res && !m_pModel->animations.empty())
        {
            m_maxAnimationTime = 0.0f;
            m_minAnimationTime = std::numeric_limits<float>::max();

            assert(m_pModel->animations.size() == 1);

            for (int i = 0; i < m_pModel->animations[0].samplers.size(); i++)
            {
                AnimationSampler animSampler;

                tinygltf::Accessor timeKeysAccessor = m_pModel->accessors[m_pModel->animations[0].samplers[i].input];

                assert(timeKeysAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
                assert(timeKeysAccessor.type == TINYGLTF_TYPE_SCALAR);

                tinygltf::BufferView timeKeysView = m_pModel->bufferViews[timeKeysAccessor.bufferView];
                tinygltf::Buffer timeKeysBuffer = m_pModel->buffers[timeKeysView.buffer];

                const float* timeKeys = reinterpret_cast<const float*>(timeKeysBuffer.data.data() + timeKeysView.byteOffset + timeKeysAccessor.byteOffset);

                animSampler.timeKeys.resize(timeKeysAccessor.count);
                for (int j = 0; j < timeKeysAccessor.count; j++)
                {
                    animSampler.timeKeys[j] = timeKeys[j];
                }

                m_maxAnimationTime = std::max(m_maxAnimationTime, animSampler.timeKeys.back());
                m_minAnimationTime = std::min(m_minAnimationTime, animSampler.timeKeys.front());

                tinygltf::Accessor keysAccessor = m_pModel->accessors[m_pModel->animations[0].samplers[i].output];

                assert(keysAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
                assert(keysAccessor.type == TINYGLTF_TYPE_VEC3 || keysAccessor.type == TINYGLTF_TYPE_VEC4);

                tinygltf::BufferView keysView = m_pModel->bufferViews[keysAccessor.bufferView];
                tinygltf::Buffer keysBuffer = m_pModel->buffers[keysView.buffer];

                const Point3f* keys3f = nullptr;
                const Point4f* keys4f = nullptr;

                if (keysAccessor.type == TINYGLTF_TYPE_VEC3)
                {
                    keys3f = reinterpret_cast<const Point3f*>(keysBuffer.data.data() + keysView.byteOffset + keysAccessor.byteOffset);
                }
                else if (keysAccessor.type == TINYGLTF_TYPE_VEC4)
                {
                    keys4f = reinterpret_cast<const Point4f*>(keysBuffer.data.data() + keysView.byteOffset + keysAccessor.byteOffset);
                }

                assert(timeKeysAccessor.count == keysAccessor.count);
                animSampler.keys.resize(keysAccessor.count);
                for (int j = 0; j < timeKeysAccessor.count; j++)
                {
                    if (keys3f)
                    {
                        animSampler.keys[j] = Point4f(keys3f[j].x, keys3f[j].y, keys3f[j].z, 0.0f);
                    }
                    else if (keys4f)
                    {
                        animSampler.keys[j] = keys4f[j];
                    }
                }

                m_animationSamplers.push_back(animSampler);
            }

            for (auto& animSampler : m_animationSamplers)
            {
                for (auto& timeKey : animSampler.timeKeys)
                {
                    timeKey -= m_minAnimationTime;
                }
            }
            m_maxAnimationTime -= m_minAnimationTime;

            for (int i = 0; i < m_pModel->animations[0].channels.size(); i++)
            {
                AnimationChannel animChannel;

                if (m_pModel->animations[0].channels[i].target_path == "rotation")
                {
                    animChannel.type = AnimationChannel::Rotation;
                }
                else if (m_pModel->animations[0].channels[i].target_path == "translation")
                {
                    animChannel.type = AnimationChannel::Translation;
                }
                else if (m_pModel->animations[0].channels[i].target_path == "scale")
                {
                    animChannel.type = AnimationChannel::Scale;
                }
                animChannel.animSamplerIdx = m_pModel->animations[0].channels[i].sampler;
                animChannel.nodeIdx = m_pModel->animations[0].channels[i].target_node;

                m_animationChannels.push_back(animChannel);
            }
        }
    }
    else if (m_modelTextures.size() < m_pModel->images.size())
    {
        // Process texture loading
        Platform::GPUResource texture;
        res = BeginGeometryCreation();
        if (res)
        {
            res = ScanTexture(m_pModel->images[m_modelTextures.size()], m_modelSRGB[m_modelTextures.size()], texture);
            if (res)
            {
                m_modelTextures.push_back(texture);
            }
            EndGeometryCreation();
        }
    }
    else
    {
        res = BeginGeometryCreation();
        if (res)
        {
            m_rootNodeIdx = m_pModel->scenes[0].nodes[0];

            res = ScanNode(*m_pModel, m_pModel->scenes[0].nodes[0], m_modelTextures);
            if (res)
            {
                m_nodeInvBindMatrices.resize(m_nodes.size(), Matrix4f());

                UpdateMatrices();
            }
            EndGeometryCreation();
        }

        delete m_pModel;
        m_pModel = nullptr;

        m_modelsToLoad.pop();
    }

    return res;
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

bool Renderer::LoadModel(const std::wstring& name, tinygltf::Model** ppModel)
{
    *ppModel = new tinygltf::Model();
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    char buffer[MAX_PATH + 1];
    sprintf(buffer, "../Common/Models/%ls/scene.gltf", name.c_str());

    bool ret = loader.LoadASCIIFromFile(*ppModel, &err, &warn, buffer);

    return ret;
}

bool Renderer::ScanTexture(const tinygltf::Image& image, bool srgb, Platform::GPUResource& texture)
{
    Platform::CreateTextureParams params;
    params.width = image.width;
    params.height = image.height;
    assert(image.component == 4);
    assert(image.bits == 8);
    assert(image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE);
    params.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    return Platform::CreateTexture(params, true, GetDevice(), texture, image.image.data(), image.image.size());
}

void Renderer::UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent)
{
    const Node& node = m_nodes[nodeIdx];

    Matrix4f m;

    if (node.useMatrix)
    {
        m = node.matrix;
    }
    else
    {
        Matrix4f rotationMatrix;
        Matrix4f translationMatrix;
        Matrix4f scaleMatrix;

        rotationMatrix.FromQuaternion(node.transform.rotation);
        translationMatrix.Offset(node.transform.translation);
        scaleMatrix.Scale(node.transform.scale.x, node.transform.scale.y, node.transform.scale.z);

        m = scaleMatrix * rotationMatrix * translationMatrix;
    }

    m = m * parent;

    if (nodeIdx >= m_nodeMatrices.size())
    {
        m_nodeMatrices.resize(nodeIdx + 1);
        m_nodeNormalMatrices.resize(nodeIdx + 1);
    }

    Matrix4f fullMatrix = m_nodeInvBindMatrices[nodeIdx] * m;

    m_nodeMatrices[nodeIdx] = fullMatrix;
    m_nodeNormalMatrices[nodeIdx] = fullMatrix.Inverse().Transpose();

    for (auto idx : node.children)
    {
        UpdateNodeMatrices(idx, m);
    }
}

void Renderer::UpdateMatrices()
{
    Matrix4f m;
    m.Identity();
    // AAV TEMP
    //m.Scale(0.01f, 0.01f, 0.01f);
    m.m[0] = -m.m[0];
    UpdateNodeMatrices(m_rootNodeIdx, m);
}

void Renderer::ApplyAnimation(float time)
{
    float localTime = time;
    while (localTime > m_maxAnimationTime)
    {
        localTime -= int(localTime / m_maxAnimationTime) * m_maxAnimationTime;
    }

    for (int i = 0; i < m_animationChannels.size(); i++)
    {
        const AnimationSampler& sampler = m_animationSamplers[m_animationChannels[i].animSamplerIdx];

        Point4f value;
        if (sampler.timeKeys.size() == 1)
        {
            value = sampler.timeKeys[0];
        }
        else
        {
            int idx0, idx1;
            float ratio;

            if (localTime < sampler.timeKeys.front())
            {
                idx0 = (int)sampler.keys.size() - 1;
                idx1 = 0;

                ratio = localTime / sampler.timeKeys.front();
            }
            else
            {
                for (int j = 1; j < sampler.timeKeys.size(); j++)
                {
                    if (localTime < sampler.timeKeys[j])
                    {
                        idx0 = j - 1;
                        idx1 = j;

                        ratio = (localTime - sampler.timeKeys[j - 1]) / (sampler.timeKeys[j] - sampler.timeKeys[j - 1]);

                        break;
                    }
                }
            }

            if (m_animationChannels[i].type == AnimationChannel::Rotation)
            {
                value = Point4f::Slerp(sampler.keys[idx0], sampler.keys[idx1], ratio);
            }
            else
            {
                value = sampler.keys[idx0] * (1.0f - ratio) + sampler.keys[idx1] * ratio;
            }
        }

        if (m_nodes[m_animationChannels[i].nodeIdx].useMatrix == true)
        {
            m_nodes[m_animationChannels[i].nodeIdx].transform.rotation = Point4f(0,0,0,1);
            m_nodes[m_animationChannels[i].nodeIdx].transform.translation = Point3f(0, 0, 0);
            m_nodes[m_animationChannels[i].nodeIdx].transform.scale = Point3f(1, 1, 1);

            m_nodes[m_animationChannels[i].nodeIdx].useMatrix = false;
        }
        switch (m_animationChannels[i].type)
        {
            case AnimationChannel::Rotation:
                m_nodes[m_animationChannels[i].nodeIdx].transform.rotation = value;
                break;

            case AnimationChannel::Translation:
                m_nodes[m_animationChannels[i].nodeIdx].transform.translation = value;
                break;

            case AnimationChannel::Scale:
                m_nodes[m_animationChannels[i].nodeIdx].transform.scale = value;
                break;
        }
    }
}

bool Renderer::ScanNode(const tinygltf::Model& model, int nodeIdx, const std::vector<Platform::GPUResource>& textures)
{
    const tinygltf::Node& node = model.nodes[nodeIdx];

    struct NormalVertex
    {
        Point3f pos;
        Point3f normal;
        Point4f tangent;
        Point2f uv;
    };

    struct NormalWeightedVertex
    {
        Point3f pos;
        Point3f normal;
        Point4f tangent;
        Point2f uv;
        Point4<unsigned short> joints;
        Point4f weights;
    };

    static int already = 0;

    bool res = true;

    if (nodeIdx >= m_nodes.size())
    {
        m_nodes.resize(nodeIdx + 1);
    }

    if (node.matrix.size() != 0)
    {
        assert(node.matrix.size() == 16);
        Matrix4f nodeMatrix;
        for (int i = 0; i < 16; i++)
        {
            nodeMatrix.m[i] = (float)node.matrix[i];
        }
        m_nodes[nodeIdx] = Node(nodeMatrix);
    }
    else
    {
        Point4f r = Point4f(0, 0, 0, 1);    // Identity rotation
        Point3f t = Point3f(0, 0, 0);       // No translation
        Point3f s = Point3f(1, 1, 1);       // No scaling

        if (node.rotation.size() != 0)
        {
            assert(node.rotation.size() == 4);
            r = Point4f( (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3] );
        }
        if (node.translation.size() != 0)
        {
            assert(node.translation.size() == 3);
            t = Point3f((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
        }
        if (node.scale.size() != 0)
        {
            assert(node.scale.size() == 3);
            s = Point3f((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
        }

        m_nodes[nodeIdx] = Node(r, t, s);
    }

    if (node.mesh != -1 /*&& already < 5*/)
    {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];

        for (int i = 0; i < mesh.primitives.size(); i++)
        {
            const tinygltf::Primitive& prim = mesh.primitives[i];
            assert(prim.mode == TINYGLTF_MODE_TRIANGLES);

            int posIdx = prim.attributes.find("POSITION")->second;
            int normIdx = prim.attributes.find("NORMAL")->second;
            int uvIdx = prim.attributes.find("TEXCOORD_0")->second;
            int tgIdx = -1;
            if (prim.attributes.find("TANGENT") != prim.attributes.end())
            {
                tgIdx = prim.attributes.find("TANGENT")->second;
            }
            int jointsIdx = -1;
            if (prim.attributes.find("JOINTS_0") != prim.attributes.end())
            {
                jointsIdx = prim.attributes.find("JOINTS_0")->second;
            }
            int weightsIdx = -1;
            if (prim.attributes.find("WEIGHTS_0") != prim.attributes.end())
            {
                weightsIdx = prim.attributes.find("WEIGHTS_0")->second;
            }
            int idxIdx = prim.indices;

            const tinygltf::Accessor& pos = model.accessors[posIdx];
            const tinygltf::Accessor& norm = model.accessors[normIdx];
            const tinygltf::Accessor& uv = model.accessors[uvIdx];
            const tinygltf::Accessor& indices = model.accessors[idxIdx];

            const tinygltf::Accessor* pTg = tgIdx == -1 ? nullptr : &model.accessors[tgIdx];
            const tinygltf::Accessor* pJoints = jointsIdx == -1 ? nullptr : &model.accessors[jointsIdx];
            const tinygltf::Accessor* pWeights = weightsIdx == -1 ? nullptr : &model.accessors[weightsIdx];

            assert(pos.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(pos.type == TINYGLTF_TYPE_VEC3);
            assert(indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT);
            assert(indices.type == TINYGLTF_TYPE_SCALAR);

            assert(pJoints == nullptr || pJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
            assert(pJoints == nullptr || pJoints->type == TINYGLTF_TYPE_VEC4);
            assert(pWeights == nullptr || pWeights->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(pWeights == nullptr || pWeights->type == TINYGLTF_TYPE_VEC4);
            assert(pTg == nullptr || pTg->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(pTg == nullptr || pTg->type == TINYGLTF_TYPE_VEC4);

            const tinygltf::BufferView& posView = model.bufferViews[pos.bufferView];
            const tinygltf::BufferView& normView = model.bufferViews[norm.bufferView];
            const tinygltf::BufferView& uvView = model.bufferViews[uv.bufferView];
            const tinygltf::BufferView& indicesView = model.bufferViews[indices.bufferView];

            const tinygltf::BufferView* pTgView = tgIdx == -1 ? nullptr : &model.bufferViews[pTg->bufferView];
            const tinygltf::BufferView* pJointsView = jointsIdx == -1 ? nullptr : &model.bufferViews[pJoints->bufferView];
            const tinygltf::BufferView* pWeightsView = weightsIdx == -1 ? nullptr : &model.bufferViews[pWeights->bufferView];

            const UINT32* pIndices = reinterpret_cast<const UINT32*>(model.buffers[indicesView.buffer].data.data() + indicesView.byteOffset + indices.byteOffset);
            const Point3f* pPos = reinterpret_cast<const Point3f*>(model.buffers[posView.buffer].data.data() + posView.byteOffset + pos.byteOffset);
            const Point3f* pNorm = reinterpret_cast<const Point3f*>(model.buffers[normView.buffer].data.data() + normView.byteOffset + norm.byteOffset);
            const Point2f* pUV = reinterpret_cast<const Point2f*>(model.buffers[uvView.buffer].data.data() + uvView.byteOffset + uv.byteOffset);

            const Point4f* pTang = tgIdx == -1 ? nullptr : reinterpret_cast<const Point4f*>(model.buffers[pTgView->buffer].data.data() + pTgView->byteOffset + pTg->byteOffset);
            const Point4<unsigned short>* pJointsValues = jointsIdx == -1 ? nullptr : reinterpret_cast<const Point4<unsigned short>*>(model.buffers[pJointsView->buffer].data.data() + pJointsView->byteOffset + pJoints->byteOffset);
            const Point4f* pWeightsValues = weightsIdx == -1 ? nullptr : reinterpret_cast<const Point4f*>(model.buffers[pWeightsView->buffer].data.data() + pWeightsView->byteOffset + pWeights->byteOffset);

            std::vector<NormalVertex> vertices;
            std::vector<NormalWeightedVertex> weightVertices;

            if (pJointsValues == nullptr)
            {
                vertices.resize(pos.count);
                for (int i = 0; i < pos.count; i++)
                {
                    vertices[i].pos = pPos[i];
                    vertices[i].normal = pNorm[i];
                    if (pTang != nullptr)
                    {
                        vertices[i].tangent = pTang[i];
                    }
                    vertices[i].uv = pUV[i];
                }
            }
            else
            {
                weightVertices.resize(pos.count);
                for (int i = 0; i < pos.count; i++)
                {
                    weightVertices[i].pos = pPos[i];
                    weightVertices[i].normal = pNorm[i];
                    if (pTang != nullptr)
                    {
                        weightVertices[i].tangent = pTang[i];
                    }
                    weightVertices[i].uv = pUV[i];

                    weightVertices[i].joints = pJointsValues[i];
                    weightVertices[i].weights = pWeightsValues[i];
                }
            }

            Geometry geometry;
            CreateGeometryParams params;

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
            params.geomAttributes.push_back({ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 24 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 40 });
            if (pJointsValues != nullptr)
            {
                params.geomAttributes.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_UINT, 48 });
                params.geomAttributes.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 56 });

                params.shaderDefines.push_back("SKINNED");
            }

            params.indexDataSize = (UINT)(indices.count * sizeof(UINT32));
            params.indexFormat = DXGI_FORMAT_R32_UINT;
            params.pIndices = pIndices;

            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("Material.hlsl");

            if (pJointsValues == nullptr)
            {
                params.pVertices = vertices.data();
                params.vertexDataSize = (UINT)(vertices.size() * sizeof(NormalVertex));
                params.vertexDataStride = sizeof(NormalVertex);
            }
            else
            {
                params.pVertices = weightVertices.data();
                params.vertexDataSize = (UINT)(weightVertices.size() * sizeof(NormalWeightedVertex));
                params.vertexDataStride = sizeof(NormalWeightedVertex);
            }

            params.rasterizerState.FrontCounterClockwise = TRUE;
            params.rtFormat = HDRFormat;
            params.rtFormat2 = HDRFormat;

            params.geomStaticTexturesCount = 0;

            Point4f emissiveFactor = {};
            bool blend = false;
            if (prim.material != -1)
            {
                const tinygltf::Material& material = model.materials[prim.material];
                if (material.alphaMode == "BLEND")
                {
                    blend = true;
                }
                params.geomStaticTexturesCount = 2;
                if (material.pbrMetallicRoughness.baseColorTexture.index != -1)
                {
                    const tinygltf::Texture& texture = model.textures[material.pbrMetallicRoughness.baseColorTexture.index];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);
                }
                else
                {
                    params.shaderDefines.push_back("PLAIN_COLOR");

                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }
                if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
                {
                    const tinygltf::Texture& texture = model.textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);
                }
                else
                {
                    params.shaderDefines.push_back("PLAIN_METAL_ROUGH");

                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }
                if (material.normalTexture.index != -1 && tgIdx != -1)
                {
                    params.shaderDefines.push_back("NORMAL_MAP");
                    params.geomStaticTexturesCount = 3;
                    const tinygltf::Texture& texture = model.textures[material.normalTexture.index];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);
                }
                if (material.doubleSided)
                {
                    params.rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                }
                emissiveFactor = Point4f{
                    (float)material.emissiveFactor[0],
                    (float)material.emissiveFactor[1],
                    (float)material.emissiveFactor[2],
                    0.0f
                };
                if (emissiveFactor.lengthSqr() > 0.01f)
                {
                    params.shaderDefines.push_back("EMISSIVE");
                }
            }

            if (blend)
            {
                params.blendState.RenderTarget[0].BlendEnable = TRUE;
                params.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                params.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                params.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                params.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                params.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
                params.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;

                params.depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            }

            res = CreateGeometry(params, geometry);
            if (res)
            {
                if (prim.material != -1)
                {
                    const tinygltf::Material& material = model.materials[prim.material];
                    geometry.objData.metalF0 = Point4f{
                        (float)material.pbrMetallicRoughness.baseColorFactor[0],
                        (float)material.pbrMetallicRoughness.baseColorFactor[1],
                        (float)material.pbrMetallicRoughness.baseColorFactor[2],
                        (float)material.pbrMetallicRoughness.baseColorFactor[3]
                    };

                    geometry.objData.pbr.x = (float)material.pbrMetallicRoughness.roughnessFactor;
                    geometry.objData.pbr.y = (float)material.pbrMetallicRoughness.metallicFactor;
                    geometry.objData.pbr.w = (float)pow(material.alphaCutoff, 2.2); // Take srgb texture reading into account

                    geometry.objData.pbr2.x = (float)material.occlusionTexture.strength;
                }

                geometry.objData.nodeIndex = Point4i(nodeIdx);
                geometry.objData.pbr.z = 0.04f;

                geometry.objData.emissiveFactor = emissiveFactor;

                if (blend)
                {
                    m_blendGeometries.push_back(geometry);
                }
                else
                {
                    m_geometries.push_back(geometry);
                }

                ++already;
            }
        }
    }

    for (int i = 0; i < node.children.size() && res; i++)
    {
        m_nodes[nodeIdx].children.push_back(node.children[i]);

        res = ScanNode(model, node.children[i], textures);
    }

    return res;
}
