#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformTexture.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"

// Shader includes
#include "Light.h"
#include "Luminance.h"
#include "LuminanceFinal.h"
#include "Tonemap.h"

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

}

const Point4f Renderer::BackColor = Point4f{0,0,0,1};
const DXGI_FORMAT Renderer::HDRFormat = DXGI_FORMAT_R11G11B10_FLOAT;

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 2, 7, { sizeof(Matrix4f), sizeof(Lights) })
    , CameraControlEuler()
    , m_pTextDraw(nullptr)
    , m_fpsCount(0)
    , m_prevFPS(0.0)
    , m_fps(0.0)
    , m_pLuminancePSO(nullptr)
    , m_pLuminanceRS(nullptr)
    , m_pLuminanceFinalPSO(nullptr)
    , m_pLuminanceFinalRS(nullptr)
    , m_lastUpdateDelta(0)
    , m_tonemapMode(TONEMAP_MODE_NONE)
    , m_brightLightBrightness(1.0f)
{
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
    static const int GridCells = 10;
    static const float GridStep = 1.0f;

    size_t vertexCount = 0;
    size_t indexCount = 0;
    std::vector<ColorVertex> gridVertices;
    std::vector<TextureVertex> cubeVertices;
    std::vector<UINT16> indices;

    bool res = Platform::BaseRenderer::Init(hWnd);
    if (res)
    {
        res = BeginGeometryCreation();
        if (res)
        {
            res = Platform::CreateTextureFromFile(_T("../Common/Kitty.png"), GetDevice(), m_textureResource);
        }
        if (res)
        {
            res = Platform::CreateTextureFromFile(_T("../Common/Plaster.png"), GetDevice(), m_plasterTextureResource);
        }

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
            Geometry geometry;
            CreateGeometryParams params;

            Platform::GetCubeDataSize(false, true, indexCount, vertexCount);
            cubeVertices.resize(vertexCount);
            indices.resize(indexCount);
            Platform::CreateCube(indices.data(), sizeof(TextureVertex), &cubeVertices[0].pos, nullptr, &cubeVertices[0].uv, &cubeVertices[0].normal);

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
            params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 20 });
            params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
            params.indexFormat = DXGI_FORMAT_R16_UINT;
            params.pIndices = indices.data();
            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("LightTexture.hlsl");
            params.pVertices = cubeVertices.data();
            params.vertexDataSize = (UINT)cubeVertices.size() * sizeof(TextureVertex);
            params.vertexDataStride = sizeof(TextureVertex);
            params.rtFormat = HDRFormat;

            params.geomStaticTextures.push_back(m_textureResource.pResource);
            params.geomStaticTexturesCount = 1;

            res = CreateGeometry(params, geometry);
            if (res)
            {
                m_geometries.push_back(geometry);
            }

            // Now create plane
            if (res)
            {
                params.geomStaticTextures.clear();
                params.geomStaticTextures.push_back(m_plasterTextureResource.pResource);

                static const UINT16 planeIndices[6] = {0,1,2,0,2,3};
                static const TextureVertex planeVertices[4] = {
                    {{-4, 0.1f, -2}, {0, 0}, {0, 1, 0}},
                    {{-4, 0.1f,  2}, {0, 1}, {0, 1, 0}},
                    {{ -1, 0.1f,  2}, {1, 1}, {0, 1, 0}},
                    {{ -1, 0.1f, -2}, {1, 0}, {0, 1, 0}}
                };

                params.indexDataSize = 6 * sizeof(UINT16);
                params.pIndices = planeIndices;
                params.pVertices = planeVertices;
                params.vertexDataSize = 4 * sizeof(TextureVertex);
                res = CreateGeometry(params, geometry);
                if (res)
                {
                    m_geometries.push_back(geometry);
                }
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
                params.rtFormat = HDRFormat;

                params.geomStaticTextures.clear();
                params.geomStaticTexturesCount = 0;

                res = CreateGeometry(params, geometry);
            }
            if (res)
            {
                m_geometries.push_back(geometry);
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

    return res;
}

void Renderer::Term()
{
    GetDevice()->WaitGPUIdle();

    TERM_RELEASE(m_pTextDraw);

    DestroyComputePipeline();

    for (auto geometry : m_geometries)
    {
        DestroyGeometry(geometry);
    }
    m_geometries.clear();

    DestroyGeometryState(m_tonemapGeomState);

    DestroyHDRTexture();

    GetDevice()->ReleaseGPUResource(m_textureResource);
    GetDevice()->ReleaseGPUResource(m_plasterTextureResource);

    Platform::BaseRenderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 2 * deltaSec;

    m_lastUpdateDelta = (float)(deltaSec);

    UpdateCamera(deltaSec);

    Matrix4f rotation;
    rotation.Rotation((float)m_angle, Point3f{ 0, 1, 0 });

    m_geometries[0].objData.transform = rotation;
    m_geometries[0].objData.transformNormals = rotation.Inverse().Transpose();

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

    if (BeginRender(beginParams))
    {
        // Setup VP matrix
        SceneCommon* pCommonCB = reinterpret_cast<SceneCommon*>(dynCBData[0]);

        pCommonCB->VP = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);
        pCommonCB->tonemapMode = Point4i{m_tonemapMode};

        // Setup lights data
        SetupLights(reinterpret_cast<Lights*>(dynCBData[1]));

        if (GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDSVHandle();
            GetCurrentCommandList()->OMSetRenderTargets(1, &m_hdrRTV, TRUE, &dsvHandle);

            D3D12_VIEWPORT viewport = GetViewport();
            GetCurrentCommandList()->RSSetViewports(1, &viewport);
            GetCurrentCommandList()->RSSetScissorRects(1, &rect);

            FLOAT clearColor[4] = { BackColor.x, BackColor.y, BackColor.z, BackColor.w };
            GetCurrentCommandList()->ClearRenderTargetView(m_hdrRTV, clearColor, 1, &rect);

            for (const auto& geometry : m_geometries)
            {
                RenderGeometry(geometry);
            }

            GetDevice()->TransitResourceState(GetCurrentCommandList(), m_hdrRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            SetBackBufferRT(); // Return current back buffer as render target

            MeasureLuminance();

            Tonemap();

            m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("FPS: %5.2f"), m_fps);
            switch (m_tonemapMode)
            {
                case TONEMAP_MODE_NONE:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("0 - no tonemapping"));
                    break;

                case TONEMAP_MODE_NORMALIZE:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("1 - normalized"));
                    break;

                case TONEMAP_MODE_REINHARD_SIMPLE:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("2 - Simple non-linear"));
                    break;

                case TONEMAP_MODE_UNCHARTED2:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("3 - Uncharted2"));
                    break;

                case TONEMAP_MODE_UNCHARTED2_SRGB:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("4 - Uncharted2 with sRGB"));
                    break;

                case TONEMAP_MODE_UNCHARTED2_SRGB_EYE_ADAPTATION:
                    m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("5 - Uncharted2 with sRGB + eye adaptation"));
                    break;
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
        case '0':
            m_tonemapMode = TONEMAP_MODE_NONE;
            return true;
            break;

        case '1':
            m_tonemapMode = TONEMAP_MODE_NORMALIZE;
            return true;
            break;

        case '2':
            m_tonemapMode = TONEMAP_MODE_REINHARD_SIMPLE;
            return true;
            break;

        case '3':
            m_tonemapMode = TONEMAP_MODE_UNCHARTED2;
            return true;
            break;

        case '4':
            m_tonemapMode = TONEMAP_MODE_UNCHARTED2_SRGB;
            return true;
            break;

        case '5':
            m_tonemapMode = TONEMAP_MODE_UNCHARTED2_SRGB_EYE_ADAPTATION;
            return true;
            break;

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
        pLights->lights[idx].pos = Point3f{ -1.5f, 0.5f, 0 };
        pLights->lights[idx].color = Point4f{ m_brightLightBrightness, 0.0f, 0, 0 };

        ++idx;
    }

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

    pLights->lightCount = idx;
}

bool Renderer::CreateHDRTexture()
{
    assert(m_hdrRT.pResource == nullptr);

    UINT height = GetRect().bottom - GetRect().top;
    UINT width = GetRect().right - GetRect().left;

    Platform::CreateTextureParams params;
    //params.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    params.format = DXGI_FORMAT_R11G11B10_FLOAT;
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
