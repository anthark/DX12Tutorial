#include "stdafx.h"
#include "PlatformCubemapBuilder.h"

#include "PlatformTexture.h"
#include "PlatformUtil.h"

#include "stb_image.h"

namespace Platform
{

CubemapBuilder::CubemapBuilder()
    : m_pRenderer(nullptr)
    , m_cubeMapRT()
    , m_textureForDelete()
    , m_builtLocalCubemaps(0)
    , m_cubeMapDS()
{
}

CubemapBuilder::~CubemapBuilder()
{
    assert(m_cubemaps.empty());
    assert(m_cubeMapRT.pResource == nullptr);
    assert(m_pRenderer == nullptr);
    assert(m_hdriFiles.empty());
}

bool CubemapBuilder::Init(BaseRenderer* pRenderer, const std::vector<std::tstring>& hdriFiles, const InitParams& params)
{
    m_pRenderer = pRenderer;

    bool res = m_pRenderer->GetDevice()->AllocateRenderTargetView(m_cubeMapRTV);
    if (res)
    {
        res = CreateCubeMapRT(params.cubemapRes);
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/IrradianceConvolution.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.depthStencilState.DepthEnable = TRUE;
        geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

        res = m_pRenderer->CreateGeometryState(geomStateParams, m_irradianceConvolutionState);

        if (res)
        {
            geomStateParams.shaderDefines.push_back("ALPHA_CUTOFF");
            geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;
            geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;

            res = m_pRenderer->CreateGeometryState(geomStateParams, m_irradianceConvolutionStateAlphaCutoff);
        }
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/EnvironmentConvolution.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.depthStencilState.DepthEnable = TRUE;
        geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

        res = m_pRenderer->CreateGeometryState(geomStateParams, m_environmentConvolutionState);

        if (res)
        {
            geomStateParams.shaderDefines.push_back("ALPHA_CUTOFF");
            geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;
            geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;

            res = m_pRenderer->CreateGeometryState(geomStateParams, m_environmentConvolutionStateAlphaCutoff);
        }
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/SimpleCopy.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        geomStateParams.dsFormat = DXGI_FORMAT_UNKNOWN;
        geomStateParams.depthStencilState.DepthEnable = FALSE;

        res = m_pRenderer->CreateGeometryState(geomStateParams, m_simpleCopyState);
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/SimpleCopy.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        geomStateParams.dsFormat = DXGI_FORMAT_UNKNOWN;
        geomStateParams.depthStencilState.DepthEnable = FALSE;

        geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;
        geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;

        res = m_pRenderer->CreateGeometryState(geomStateParams, m_simpleCopyStateAlpha);
    }

    if (res)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/EquirectToCubemapFace.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.depthStencilState.DepthEnable = TRUE;
        geomStateParams.rtFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

        res = m_pRenderer->CreateGeometryState(geomStateParams, m_equirectToCubemapFaceState);
    }

    if (res)
    {
        m_hdriFiles = hdriFiles;

        assert(params.irradianceRes <= params.cubemapRes && params.envRes <= params.cubemapRes);

        m_params = params;
    }

    return res;
}

bool CubemapBuilder::InitLocal(const InitLocalParams& localParams)
{
    m_localCubemapParams = localParams;

    assert(m_cubeMapDS.pResource == nullptr);

    UINT rtRes = (UINT)m_cubeMapRT.pResource->GetDesc().Width;

    // Create DS resource for local cubemap rendering
    Platform::CreateTextureParams params;
    params.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    params.height = rtRes;
    params.width = rtRes;
    params.enableDS = true;
    params.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), m_cubeMapDS);

    if (res)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC desc;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Flags = D3D12_DSV_FLAG_NONE;

        m_pRenderer->GetDevice()->GetDXDevice()->CreateDepthStencilView(m_cubeMapDS.pResource, &desc, m_localCubemapParams.dsvHandle);
    }

    // Create temporary cubemap for local cubemap generation
    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = m_params.cubemapRes;
        params.width = m_params.cubemapRes;
        params.arraySize = 6;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        params.mips = m_params.cubemapMips;

        res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), m_tempLocalCubemap);
    }

    // Create arrays of irradiance and environment cubemaps
    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = m_localCubemapParams.irradianceRes;
        params.width = m_localCubemapParams.irradianceRes;
        params.arraySize = 6 * m_localCubemapParams.grid.x * m_localCubemapParams.grid.y;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        params.mips = 1;

        res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), m_localCubemapIrradianceArray);
    }
    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = m_localCubemapParams.envRes;
        params.width = m_localCubemapParams.envRes;
        params.arraySize = 6 * m_localCubemapParams.grid.x * m_localCubemapParams.grid.y;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        params.mips = m_localCubemapParams.roughnessMips;

        res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), m_localCubemapEnvironmentArray);
    }

    return true;
}

void CubemapBuilder::Term()
{
    if (m_cubeMapDS.pResource != nullptr)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(m_cubeMapDS);
    }
    if (m_tempLocalCubemap.pResource != nullptr)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(m_tempLocalCubemap);
    }
    if (m_localCubemapIrradianceArray.pResource != nullptr)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(m_localCubemapIrradianceArray);
    }
    if (m_localCubemapEnvironmentArray.pResource != nullptr)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(m_localCubemapEnvironmentArray);
    }

    for (auto& cubemap : m_cubemaps)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(cubemap.cubemap);
        m_pRenderer->GetDevice()->ReleaseGPUResource(cubemap.irradianceMap);
        m_pRenderer->GetDevice()->ReleaseGPUResource(cubemap.envMap);
    }
    m_cubemaps.clear();

    m_pRenderer->DestroyGeometryState(m_equirectToCubemapFaceState);
    m_pRenderer->DestroyGeometryState(m_simpleCopyState);
    m_pRenderer->DestroyGeometryState(m_simpleCopyStateAlpha);
    m_pRenderer->DestroyGeometryState(m_environmentConvolutionState);
    m_pRenderer->DestroyGeometryState(m_environmentConvolutionStateAlphaCutoff);
    m_pRenderer->DestroyGeometryState(m_irradianceConvolutionState);
    m_pRenderer->DestroyGeometryState(m_irradianceConvolutionStateAlphaCutoff);

    DestroyCubeMapRT();

    m_pRenderer = nullptr;
}

bool CubemapBuilder::RenderCubemap()
{
    // Zero step - destroy prev cubemap
    if (m_textureForDelete.pResource != nullptr)
    {
        m_pRenderer->GetDevice()->WaitGPUIdle();
        m_pRenderer->GetDevice()->ReleaseGPUResource(m_textureForDelete);

        m_textureForDelete = Platform::GPUResource();

        m_hdriFiles.erase(m_hdriFiles.begin());
    }
    if (m_hdriFiles.empty())
    {
        return true;
    }

    // First step - load HDRI file
    Platform::GPUResource equirectCubemap = {};
    bool res = m_pRenderer->BeginGeometryCreation();
    if (res)
    {
        res = LoadHDRTexture(m_hdriFiles.front().c_str(), &equirectCubemap);

        m_pRenderer->EndGeometryCreation();
    }
    if (res) // Don't forget to delete it after all cubemap building procedure!
    {
        m_textureForDelete = equirectCubemap;
    }

    // Second step - create cubemap
    if (res)
    {
        Platform::CreateTextureParams params;
        params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        params.height = m_params.cubemapRes;
        params.width = m_params.cubemapRes;
        params.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        params.arraySize = 6; // For cubemap
        params.mips = m_params.cubemapMips;

        Platform::CreateTextureParams paramsIrradiance;
        paramsIrradiance.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        paramsIrradiance.height = m_params.irradianceRes;
        paramsIrradiance.width = m_params.irradianceRes;
        paramsIrradiance.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        paramsIrradiance.arraySize = 6; // For irradiance cubemap

        Platform::CreateTextureParams paramsEnvironment;
        paramsEnvironment.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        paramsEnvironment.height = m_params.envRes;
        paramsEnvironment.width = m_params.envRes;
        paramsEnvironment.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        paramsEnvironment.arraySize = 6; // For environment cubemap
        paramsEnvironment.mips = m_params.roughnessMips;

        Cubemap cubemap;
        res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), cubemap.cubemap);
        if (res)
        {
            res = Platform::CreateTexture(paramsIrradiance, false, m_pRenderer->GetDevice(), cubemap.irradianceMap);
        }
        if (res)
        {
            res = Platform::CreateTexture(paramsEnvironment, false, m_pRenderer->GetDevice(), cubemap.envMap);
        }
        if (res)
        {
            m_cubemaps.push_back(cubemap);
        }
    }

    // Third step - queue cubemap rendering
    if (res)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        res = m_pRenderer->GetDevice()->AllocateDynamicDescriptors(3, cpuHandle, gpuHandle);

        if (res)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
            texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            texDesc.Format = equirectCubemap.pResource->GetDesc().Format;
            texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            texDesc.Texture2D.MipLevels = 1;
            m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(equirectCubemap.pResource, &texDesc, cpuHandle);

            res = m_pRenderer->RenderToCubemap(m_cubemaps.back().cubemap, m_cubeMapRT, m_cubeMapRTV, m_equirectToCubemapFaceState, gpuHandle, m_params.cubemapRes, 0, m_params.cubemapMips);
            if (res)
            {
                res = BuildCubemapMips(m_cubemaps.back().cubemap, false);
            }
            if (res)
            {
                cpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                gpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
            }

            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_cubemaps.back().cubemap.pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = 1;
                m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(m_cubemaps.back().cubemap.pResource, &texDesc, cpuHandle);

                res = m_pRenderer->RenderToCubemap(m_cubemaps.back().irradianceMap, m_cubeMapRT, m_cubeMapRTV, m_irradianceConvolutionState, gpuHandle, m_params.irradianceRes, 0, 1);
            }
            if (res)
            {
                cpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                gpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
            }

            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_cubemaps.back().cubemap.pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = m_params.cubemapMips;
                texDesc.Texture2D.MostDetailedMip = 0;
                m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(m_cubemaps.back().cubemap.pResource, &texDesc, cpuHandle);

                for (int i = 0; i < m_params.roughnessMips && res; i++)
                {
                    res = m_pRenderer->RenderToCubemap(m_cubemaps.back().envMap, m_cubeMapRT, m_cubeMapRTV, m_environmentConvolutionState, gpuHandle, m_params.envRes, i, m_params.roughnessMips);
                }
            }
        }
    }

    if (res)
    {
        m_loadedCubemaps.push_back(ToString(StripExtension(ShortFilename(m_hdriFiles.front()))));
    }

    return res;
}

bool CubemapBuilder::RenderLocalCubemap()
{
    const Point3f CameraDirs[6] = {
        Point3f{(float)0, (float)M_PI, 0}, // +X
        Point3f{(float)0, 0, 0}, // -X
        Point3f{-(float)M_PI / 2, 0, (float)M_PI / 2}, // +Y
        Point3f{(float)M_PI / 2, 0, -(float)M_PI / 2}, // -Y
        Point3f{0, -(float)M_PI / 2, 0}, // +Z
        Point3f{0, (float)M_PI / 2, 0} // -Z
    };

    bool res = true;

    int cubemapIdx = (int)m_builtLocalCubemaps;
    if (cubemapIdx >= m_localCubemapParams.grid.x * m_localCubemapParams.grid.y)
    {
        cubemapIdx = 0;
    }

    Point2f cubemapPos = Point2f{ (float)(cubemapIdx % m_localCubemapParams.grid.x), (float)(cubemapIdx / m_localCubemapParams.grid.x) };
    Point3f pos = Point3f{ m_localCubemapParams.pos.x + m_localCubemapParams.size * (0.5f + cubemapPos.x)
        , m_localCubemapParams.size * 0.5f
        , m_localCubemapParams.pos.y + m_localCubemapParams.size * (0.5f + cubemapPos.y)
    };

    if (m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_tempLocalCubemap.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST))
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_localCubemapParams.dsvHandle;
        m_pRenderer->GetCurrentCommandList()->OMSetRenderTargets(1, &m_cubeMapRTV, TRUE, &dsvHandle);

        int pixels = m_params.cubemapRes;

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (FLOAT)pixels;
        viewport.Height = (FLOAT)pixels;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        m_pRenderer->GetCurrentCommandList()->RSSetViewports(1, &viewport);

        D3D12_RECT rect = {};
        rect.bottom = pixels;
        rect.right = pixels;
        m_pRenderer->GetCurrentCommandList()->RSSetScissorRects(1, &rect);

        FLOAT clearColor[4] = { 0,0,0,0 };

        Platform::Camera camera;
        camera.SetDistance(1.0f);
        camera.SetHorzFOV((float)M_PI / 2);
        camera.SetNear(1.0f);
        camera.SetFar(1000.0f);

        // Render 6 camera views of current scene
        for (int i = 0; i < 6 && res; i++)
        {
            m_pRenderer->GetCurrentCommandList()->ClearRenderTargetView(m_cubeMapRTV, clearColor, 1, &rect);
            m_pRenderer->GetCurrentCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

            if (res)
            {
                camera.SetLat(CameraDirs[i].x);
                camera.SetLon(CameraDirs[i].y);
                camera.SetRoll(CameraDirs[i].z);
                Point3f dir = -Point3f{ cosf(CameraDirs[i].y) * cosf(CameraDirs[i].x), sinf(CameraDirs[i].x), sinf(CameraDirs[i].y) * cosf(CameraDirs[i].x) };
                dir.normalize();
                camera.SetLookAt(pos + dir);

                res = m_pRenderer->RenderScene(camera);
            }
            if (res)
            {
                res = m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                if (res)
                {
                    CD3DX12_TEXTURE_COPY_LOCATION dstLoc{ m_tempLocalCubemap.pResource, (UINT)(i * m_params.cubemapMips + 0) };
                    CD3DX12_TEXTURE_COPY_LOCATION srcLoc{ m_cubeMapRT.pResource };

                    D3D12_BOX rect = {};
                    rect.front = 0;
                    rect.back = 1;
                    rect.right = rect.bottom = pixels;

                    m_pRenderer->GetCurrentCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &rect);

                    m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
            }
        }

        m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_tempLocalCubemap.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // Create mips
        if (res)
        {
            res = BuildCubemapMips(m_tempLocalCubemap, true);
        }
        if (res)
        {
            // Calculate irradiance map
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            res = m_pRenderer->GetDevice()->AllocateDynamicDescriptors(2, cpuHandle, gpuHandle);
            {
                PIX_MARKER_CMDLIST_SCOPE_STR(m_pRenderer->GetCurrentCommandList(), Irradiance, _T("IrradianceMap"));
                if (res)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                    texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    texDesc.Format = m_tempLocalCubemap.pResource->GetDesc().Format;
                    texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    texDesc.Texture2D.MipLevels = 1;
                    m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(m_tempLocalCubemap.pResource, &texDesc, cpuHandle);

                    res = m_pRenderer->RenderToCubemap(m_localCubemapIrradianceArray, m_cubeMapRT, m_cubeMapRTV, m_irradianceConvolutionStateAlphaCutoff, gpuHandle, m_localCubemapParams.irradianceRes, 0, 1, (int)(cubemapIdx * 6));
                }
                if (res)
                {
                    cpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                    gpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                }
            }
            {
                PIX_MARKER_CMDLIST_SCOPE_STR(m_pRenderer->GetCurrentCommandList(), Environment, _T("EnvironmentMap"));

                // Calculate environment map
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_tempLocalCubemap.pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                texDesc.Texture2D.MipLevels = m_localCubemapParams.roughnessMips;
                texDesc.Texture2D.MostDetailedMip = 0;
                m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(m_tempLocalCubemap.pResource, &texDesc, cpuHandle);

                for (int i = 0; i < m_params.roughnessMips && res; i++)
                {
                    res = m_pRenderer->RenderToCubemap(m_localCubemapEnvironmentArray, m_cubeMapRT, m_cubeMapRTV, m_environmentConvolutionStateAlphaCutoff, gpuHandle, m_localCubemapParams.envRes, i, m_localCubemapParams.roughnessMips, (int)(cubemapIdx * 6 * m_localCubemapParams.roughnessMips));
                }
                if (res)
                {
                    cpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                    gpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
                }
            }
        }

        if (res)
        {
            if (m_builtLocalCubemaps < m_localCubemapParams.grid.x * m_localCubemapParams.grid.y)
            {
                ++m_builtLocalCubemaps;
            }
        }

        return res;
    }
    else
    {
        return false;
    }
}

bool CubemapBuilder::CreateCubeMapRT(int rtRes)
{
    assert(m_cubeMapRT.pResource == nullptr);

    Platform::CreateTextureParams params;
    params.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    params.height = rtRes;
    params.width = rtRes;
    params.enableRT = true;
    params.initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue;
    clearValue.Format = params.format;
    memset(&clearValue.Color, 0, sizeof(clearValue.Color));
    params.pOptimizedClearValue = &clearValue;

    bool res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), m_cubeMapRT);

    if (res)
    {
        D3D12_RENDER_TARGET_VIEW_DESC desc;
        desc.Format = params.format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;

        m_pRenderer->GetDevice()->GetDXDevice()->CreateRenderTargetView(m_cubeMapRT.pResource, &desc, m_cubeMapRTV);
    }

    return res;
}

void CubemapBuilder::DestroyCubeMapRT()
{
    m_pRenderer->GetDevice()->ReleaseGPUResource(m_cubeMapRT);
}

bool CubemapBuilder::LoadHDRTexture(LPCTSTR filename, Platform::GPUResource* pResource)
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

        res = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), *pResource, pHDR4Data, width * height * 4 * sizeof(float));

        free(pHDR4Data);
    }

    if (pHDRData != nullptr)
    {
        free(pHDRData);
    }
    fclose(pFile);

    return res;
}

bool CubemapBuilder::BuildCubemapMips(const Platform::GPUResource& resource, bool alpha)
{
    m_pRenderer->GetCurrentCommandList()->OMSetRenderTargets(1, &m_cubeMapRTV, TRUE, NULL);

    UINT pixels = m_params.cubemapRes;

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
    bool res = m_pRenderer->GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        memcpy(pVertices, vertices, 4 * sizeof(Point4f));

        // Setup
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        vertexBufferView.BufferLocation = gpuVirtualAddress;
        vertexBufferView.StrideInBytes = (UINT)sizeof(Point4f);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        m_pRenderer->GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(6 * sizeof(UINT16));
        res = m_pRenderer->GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
        if (res)
        {
            memcpy(pIndices, indices, 6 * sizeof(UINT16));

            // Setup
            D3D12_INDEX_BUFFER_VIEW indexBufferView;

            indexBufferView.BufferLocation = gpuVirtualAddress;
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            m_pRenderer->GetCurrentCommandList()->IASetIndexBuffer(&indexBufferView);
        }
    }

    m_pRenderer->SetupGeometryState(alpha ? m_simpleCopyStateAlpha : m_simpleCopyState);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;

    res = m_pRenderer->GetDevice()->AllocateDynamicDescriptors(6 * (m_params.cubemapMips - 1), cpuHandle, gpuHandle);

    for (int j = 1; j < m_params.cubemapMips && res; j++)
    {
        pixels /= 2;

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (FLOAT)pixels;
        viewport.Height = (FLOAT)pixels;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        m_pRenderer->GetCurrentCommandList()->RSSetViewports(1, &viewport);

        D3D12_RECT rect = {};
        rect.bottom = pixels;
        rect.right = pixels;
        m_pRenderer->GetCurrentCommandList()->RSSetScissorRects(1, &rect);

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
            m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(resource.pResource, &texDesc, cpuHandle);

            m_pRenderer->GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, gpuHandle);

            m_pRenderer->GetCurrentCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);

            res = m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), resource.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, (UINT)j + i * m_params.cubemapMips);
            if (res)
            {
                res = m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                if (res)
                {
                    CD3DX12_TEXTURE_COPY_LOCATION dstLoc{ resource.pResource, (UINT)j + i * m_params.cubemapMips };
                    CD3DX12_TEXTURE_COPY_LOCATION srcLoc{ m_cubeMapRT.pResource };

                    D3D12_BOX rect = {};
                    rect.front = 0;
                    rect.back = 1;
                    rect.right = rect.bottom = pixels;

                    m_pRenderer->GetCurrentCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &rect);

                    m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), m_cubeMapRT.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }

                m_pRenderer->GetDevice()->TransitResourceState(m_pRenderer->GetCurrentCommandList(), resource.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (UINT)j + i * m_params.cubemapMips);
            }

            cpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
            gpuHandle.ptr += m_pRenderer->GetDevice()->GetSRVDescSize();
        }
    }

    return res;
}

} // Platform
