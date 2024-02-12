#include "stdafx.h"
#include "PlatformBaseRenderer.h"

#include "Platform.h"
#include "PlatformShaderCache.h"

namespace
{

struct CubemapVertex
{
    Point3f pos;
};

}

namespace Platform
{

BaseRenderer::BaseRenderer(Device* pDevice, UINT commonCBCount, UINT commonTexCount, const std::vector<UINT>& commonCBSizes, UINT additionalDSDescCount)
    : Renderer(pDevice)
    , m_pCurrentRootSignature(nullptr)
    , m_currentCommonTableStart{}
    , m_pCurrentRenderCommandList(nullptr)
    , m_pCurrentUploadCommandList(nullptr)
    , m_pCurrentBackBuffer(nullptr)
    , m_currentRTVHandle{}
    , m_pDSVHeap(nullptr)
    , m_depthBuffer()
    , m_pShaderCache(nullptr)
    , m_commonCBCount(commonCBCount)
    , m_commonTexCount(commonTexCount)
    , m_commonCBSizes(commonCBSizes)
    , m_additionalDSDescCount(additionalDSDescCount)
{
}

BaseRenderer::~BaseRenderer()
{
    assert(m_pCurrentRootSignature == nullptr);
    assert(m_pCurrentRenderCommandList == nullptr);
    assert(m_pCurrentUploadCommandList == nullptr);
    assert(m_pCurrentBackBuffer == nullptr);
    assert(m_pDSVHeap == nullptr);
    assert(m_pShaderCache == nullptr);
}

bool BaseRenderer::Init(HWND hWnd)
{
    bool res = Platform::Renderer::Init(hWnd);
    if (res)
    {
        m_pShaderCache = new ShaderCache();
        m_pShaderCache->Init(GetDevice());
    }
    if (res)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.NumDescriptors = 1 + m_additionalDSDescCount;
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

        HRESULT hr = S_OK;
        D3D_CHECK(GetDevice()->GetDXDevice()->CreateDescriptorHeap(&dsvDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_pDSVHeap));
        res = SUCCEEDED(hr);
    }
    if (res)
    {
        res = CreateDepthBuffer();
    }
    if (res)
    {
        m_faceTransforms.resize(6);

        Matrix4f roty;
        roty.Rotation((float)M_PI / 2, Point3f{ 0, 1, 0 });

        m_faceTransforms[0].Identity();
        m_faceTransforms[1].Rotation((float)M_PI, Point3f{ 0, 1, 0 });
        m_faceTransforms[2].Rotation(-(float)M_PI / 2, Point3f{ 0, 0, 1 });
        m_faceTransforms[2] = m_faceTransforms[2] * roty;
        m_faceTransforms[3].Rotation((float)M_PI / 2, Point3f{ 0, 0, 1 });
        m_faceTransforms[3] = m_faceTransforms[3] * roty;
        m_faceTransforms[4].Rotation((float)M_PI / 2, Point3f{ 0, 1, 0 });
        m_faceTransforms[5].Rotation(-(float)M_PI / 2, Point3f{ 0, 1, 0 });

        Platform::Camera camera;
        camera.SetLookAt(Point3f{ 0,0,0 });
        camera.SetProjection(Platform::Camera::Perspective);
        camera.SetHorzFOV((float)M_PI / 2);
        camera.SetDistance(0.0f);
        camera.SetNear(0.0001f);
        camera.SetFar(1.0f);
        camera.SetLat(0.0f);
        camera.SetLon((float)M_PI);

        m_cubemapFaceVP = camera.CalcViewMatrix() * camera.CalcProjMatrix(1);
    }

    return res;
}

void BaseRenderer::Term()
{
    if (m_pShaderCache != nullptr)
    {
        m_pShaderCache->Term();
        delete m_pShaderCache;
        m_pShaderCache = nullptr;
    }

    GetDevice()->ReleaseGPUResource(m_depthBuffer);

    D3D_RELEASE(m_pDSVHeap);

    Platform::Renderer::Term();
}

bool BaseRenderer::BeginRender(BeginRenderParams& params)
{
    D3D12_RECT rect = GetRect();

    m_pCurrentRootSignature = nullptr;
    m_currentCommonTableStart = {};

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};

    UINT commonResourceCount = m_commonCBCount + m_commonTexCount;

    bool res = true;
    if (res && commonResourceCount > 0)
    {
        res = GetDevice()->AllocateDynamicDescriptors(commonResourceCount, params.cpuTextureHandles, params.gpuTextureHandles);
        if (res)
        {
            m_currentCommonTableStart = params.gpuTextureHandles;
            res = GetDevice()->AllocateDynamicBuffers(m_commonCBCount, m_commonCBSizes.data(), params.cpuTextureHandles, params.ppCPUData);
        }
        if (res)
        {
            params.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize() * m_commonCBCount;
            params.gpuTextureHandles.ptr += GetDevice()->GetSRVDescSize() * m_commonCBCount;
        }
    }
    if (res)
    {
        res = GetDevice()->BeginRenderCommandList(&m_pCurrentRenderCommandList, &m_pCurrentBackBuffer, &m_currentRTVHandle);
    }
    if (res)
    {
        dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

        HRESULT hr = S_OK;
        m_pCurrentRenderCommandList->OMSetRenderTargets(1, &m_currentRTVHandle, TRUE, &dsvHandle);

        D3D12_VIEWPORT viewport = GetViewport();
        m_pCurrentRenderCommandList->RSSetViewports(1, &viewport);
        m_pCurrentRenderCommandList->RSSetScissorRects(1, &rect);

        ID3D12DescriptorHeap* pHeap = GetDevice()->GetDescriptorHeap();
        m_pCurrentRenderCommandList->SetDescriptorHeaps(1, &pHeap);

        m_pCurrentRootSignature = nullptr;

        res = GetDevice()->TransitResourceState(m_pCurrentRenderCommandList, m_pCurrentBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    if (res)
    {
        FLOAT clearColor[4] = { params.backColor.x, params.backColor.y, params.backColor.z, params.backColor.w };
        m_pCurrentRenderCommandList->ClearRenderTargetView(m_currentRTVHandle, clearColor, 1, &rect);
        m_pCurrentRenderCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);
    }

    return res;
}

bool BaseRenderer::RestartCommonResources(BeginRenderParams& params)
{
    m_pCurrentRootSignature = nullptr;
    m_currentCommonTableStart = {};

    UINT commonResourceCount = m_commonCBCount + m_commonTexCount;

    bool res = true;
    if (res && commonResourceCount > 0)
    {
        res = GetDevice()->AllocateDynamicDescriptors(commonResourceCount, params.cpuTextureHandles, params.gpuTextureHandles);
        if (res)
        {
            m_currentCommonTableStart = params.gpuTextureHandles;
            res = GetDevice()->AllocateDynamicBuffers(m_commonCBCount, m_commonCBSizes.data(), params.cpuTextureHandles, params.ppCPUData);
        }
        if (res)
        {
            params.cpuTextureHandles.ptr += GetDevice()->GetSRVDescSize() * m_commonCBCount;
            params.gpuTextureHandles.ptr += GetDevice()->GetSRVDescSize() * m_commonCBCount;
        }
    }

    return res;
}

void BaseRenderer::ResetRender()
{
    m_pCurrentRootSignature = nullptr;
    D3D12_RECT rect = GetRect();
    m_pCurrentRenderCommandList->RSSetScissorRects(1, &rect);
}

void BaseRenderer::EndRender(bool vsync)
{
    assert(m_pCurrentRenderCommandList != nullptr);
    assert(m_pCurrentBackBuffer != nullptr);

    GetDevice()->TransitResourceState(m_pCurrentRenderCommandList, m_pCurrentBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    GetDevice()->CloseSubmitAndPresentRenderCommandList(vsync);

    m_pCurrentRenderCommandList = nullptr;
    m_pCurrentBackBuffer = nullptr;

    m_pCurrentRootSignature = nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE BaseRenderer::GetBackBufferDSVHandle() const
{
    return m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

void BaseRenderer::SetBackBufferRT() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

    m_pCurrentRenderCommandList->OMSetRenderTargets(1, &m_currentRTVHandle, TRUE, &dsvHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE BaseRenderer::GetDSVStartHandle() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
    dsvHandle.ptr += GetDevice()->GetDSVDescSize();

    return dsvHandle;
}

bool BaseRenderer::BeginGeometryCreation()
{
    bool res = true;
    if (res)
    {
        ID3D12GraphicsCommandList* pUploadCommandList = nullptr;
        res = GetDevice()->BeginUploadCommandList(&pUploadCommandList);
        if (res)
        {
            m_pCurrentUploadCommandList = pUploadCommandList;
        }
    }

    return res;
}

bool BaseRenderer::CreateGeometryState(const GeometryStateParams& params, GeometryState& geomState)
{
    bool res = true;

    // Create root signature
    if (res)
    {
        std::vector<D3D12_ROOT_PARAMETER> rootSignatureParams;

        D3D12_DESCRIPTOR_RANGE descRangesCommon[2] = {};
        descRangesCommon[0].BaseShaderRegister = 0;
        descRangesCommon[0].NumDescriptors = m_commonCBCount;
        descRangesCommon[0].OffsetInDescriptorsFromTableStart = 0;
        descRangesCommon[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRangesCommon[0].RegisterSpace = 0;
        if (m_commonTexCount > 0)
        {
            descRangesCommon[1].BaseShaderRegister = 0;
            descRangesCommon[1].NumDescriptors = m_commonTexCount;
            descRangesCommon[1].OffsetInDescriptorsFromTableStart = m_commonCBCount;
            descRangesCommon[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            descRangesCommon[1].RegisterSpace = 0;
        }
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = m_commonTexCount > 0 ? 2 : 1;
            param.DescriptorTable.pDescriptorRanges = descRangesCommon;

            rootSignatureParams.push_back(param);
        }
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.RegisterSpace = 0;
            param.Descriptor.ShaderRegister = m_commonCBCount;

            rootSignatureParams.push_back(param);
        }
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.RegisterSpace = 0;
            param.Descriptor.ShaderRegister = m_commonCBCount + 1;

            rootSignatureParams.push_back(param);
        }

        D3D12_DESCRIPTOR_RANGE descRangesGeometry[1] = {};
        descRangesGeometry[0].BaseShaderRegister = m_commonTexCount;
        descRangesGeometry[0].NumDescriptors = (UINT)params.geomStaticTexturesCount;
        descRangesGeometry[0].OffsetInDescriptorsFromTableStart = 0;
        descRangesGeometry[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRangesGeometry[0].RegisterSpace = 0;
        if (params.geomStaticTexturesCount > 0)
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = descRangesGeometry;

            rootSignatureParams.push_back(param);
        }

        D3D12_DESCRIPTOR_RANGE descRangesGeometryDyn[1] = {};
        descRangesGeometryDyn[0].BaseShaderRegister = m_commonTexCount + (UINT)params.geomStaticTexturesCount;
        descRangesGeometryDyn[0].NumDescriptors = (UINT)params.geomDynamicTexturesCount;
        descRangesGeometryDyn[0].OffsetInDescriptorsFromTableStart = 0;
        descRangesGeometryDyn[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRangesGeometryDyn[0].RegisterSpace = 0;
        if (params.geomDynamicTexturesCount > 0)
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = descRangesGeometryDyn;

            rootSignatureParams.push_back(param);
        }

        CD3DX12_STATIC_SAMPLER_DESC staticSamplers[5] = {
            {0, D3D12_FILTER_ANISOTROPIC},
            {1, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT},
            {2, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP},
            {3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP}, // Shadow map sampler
            {4, D3D12_FILTER_COMPARISON_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP}, // Shadow map sampler for PCF
        };

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init((UINT)rootSignatureParams.size(), rootSignatureParams.data(), 5, staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &geomState.pRootSignature);
    }

    // Create shaders
    ID3DBlob* pVertexShaderBinary = nullptr;
    ID3DBlob* pPixelShaderBinary = nullptr;
    if (res)
    {
        if (m_pShaderCache != nullptr)
        {
            res = m_pShaderCache->CompileShader(params.pShaderSourceName, params.shaderDefines, Platform::Device::Vertex, &pVertexShaderBinary);
        }
        else
        {
            res = GetDevice()->CompileShader(params.pShaderSourceName, params.shaderDefines, Platform::Device::Vertex, &pVertexShaderBinary);
        }
    }
    if (res)
    {
        if (m_pShaderCache != nullptr)
        {
            res = m_pShaderCache->CompileShader(params.pShaderSourceName, params.shaderDefines, Platform::Device::Pixel, &pPixelShaderBinary);
        }
        else
        {
            res = GetDevice()->CompileShader(params.pShaderSourceName, params.shaderDefines, Platform::Device::Pixel, &pPixelShaderBinary);
        }
    }

    // Create PSO
    if (res)
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
        for (const auto& attribute : params.geomAttributes)
        {
            D3D12_INPUT_ELEMENT_DESC elementDesc = { attribute.SemanticName, attribute.SemanticIndex, attribute.Format, 0, attribute.AlignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

            inputElementDescs.push_back(elementDesc);
        }

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs.data(), (UINT)inputElementDescs.size() };
        psoDesc.pRootSignature = geomState.pRootSignature;
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderBinary);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderBinary);
        psoDesc.RasterizerState = params.rasterizerState;
        psoDesc.BlendState = params.blendState;
        psoDesc.DepthStencilState = params.depthStencilState;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = params.primTopologyType;
        psoDesc.NumRenderTargets = 0;
        if (params.rtFormat != DXGI_FORMAT_UNKNOWN)
        {
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = params.rtFormat;
        }
        if (params.rtFormat2 != DXGI_FORMAT_UNKNOWN)
        {
            psoDesc.NumRenderTargets = 2;
            psoDesc.RTVFormats[1] = params.rtFormat2;
        }
        if (params.rtFormat3 != DXGI_FORMAT_UNKNOWN)
        {
            psoDesc.NumRenderTargets = 3;
            psoDesc.RTVFormats[2] = params.rtFormat3;
        }
        if (params.rtFormat4 != DXGI_FORMAT_UNKNOWN)
        {
            psoDesc.NumRenderTargets = 4;
            psoDesc.RTVFormats[3] = params.rtFormat4;
        }
        psoDesc.DSVFormat = params.dsFormat;
        psoDesc.SampleDesc.Count = 1;

        res = GetDevice()->CreatePSO(psoDesc, &geomState.pPSO);
    }

    if (res)
    {
        geomState.primType = params.primTopologyType;

        if (GetDevice()->IsDebug())
        {
            geomState.pPSO->SetName(params.pShaderSourceName);
        }
    }

    D3D_RELEASE(pVertexShaderBinary);
    D3D_RELEASE(pPixelShaderBinary);

    return res;
}

bool BaseRenderer::CreateGeometry(const CreateGeometryParams& params, Geometry& geometry)
{
    assert(params.geomStaticTextures.size() == params.geomStaticTexturesCount);

    bool res = CreateGeometryState(params, geometry);
    if (res)
    {
        res = CreateGeometryBuffers(params, geometry);
    }

    return res;
}

bool BaseRenderer::CreateGeometrySharedState(const GeometryState& srcState, const CreateGeometryParams& params, Geometry& geometry)
{
    ((GeometryState&)geometry) = srcState;

    geometry.pPSO->AddRef();
    geometry.pRootSignature->AddRef();

    return CreateGeometryBuffers(params, geometry);
}

void BaseRenderer::DestroyGeometryState(GeometryState& geomState)
{
    D3D_RELEASE(geomState.pPSO);
    D3D_RELEASE(geomState.pRootSignature);
}

void BaseRenderer::DestroyGeometry(Geometry& geometry)
{
    DestroyGeometryState(geometry);

    GetDevice()->ReleaseGPUResource(geometry.vertexBuffer);
    GetDevice()->ReleaseGPUResource(geometry.indexBuffer);
}

bool BaseRenderer::RenderScene(const Camera& camera)
{
    assert(0); // Not implemented

    return false;
}

void BaseRenderer::EndGeometryCreation()
{
    m_pCurrentUploadCommandList = nullptr;

    GetDevice()->CloseUploadCommandList();
}

void BaseRenderer::SetupGeometryState(const GeometryState& geomState)
{
    m_pCurrentRenderCommandList->SetPipelineState(geomState.pPSO);

    if (m_pCurrentRootSignature != geomState.pRootSignature)
    {
        m_pCurrentRenderCommandList->SetGraphicsRootSignature(geomState.pRootSignature);
        if (m_currentCommonTableStart.ptr != 0)
        {
            m_pCurrentRenderCommandList->SetGraphicsRootDescriptorTable(0, m_currentCommonTableStart);
        }

        m_pCurrentRootSignature = geomState.pRootSignature;
    }

    switch (geomState.primType)
    {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
            m_pCurrentRenderCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            break;

        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            m_pCurrentRenderCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            break;

        default:
            assert(0); // Unknown topology type
            break;
    }
}

void BaseRenderer::RenderGeometry(const Geometry& geometry, const void* pInstData, size_t instDataSize, const GeometryState* pState, D3D12_GPU_DESCRIPTOR_HANDLE dynTexturesGpu, const void* pInstObjectData, size_t instObjectDataSize)
{
    if (pState != nullptr)
    {
        SetupGeometryState(*pState);
    }
    else
    {
        SetupGeometryState(geometry);
    }

    if (geometry.texturesTableStart.ptr != 0)
    {
        m_pCurrentRenderCommandList->SetGraphicsRootDescriptorTable(3, geometry.texturesTableStart);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE dynCBStartHandle = {};
    D3D12_CONSTANT_BUFFER_VIEW_DESC descs[2] = {};

    size_t splitDataSize = 0;
    const void* pSplitData = nullptr;
    if (pInstData != nullptr && instDataSize != 0)
    {
        pSplitData = pInstData;
        splitDataSize = instDataSize;
    }
    else
    {
        pSplitData = geometry.GetObjCB(splitDataSize);
    }
    if (pSplitData != nullptr && splitDataSize != 0)
    {
        UINT8* dynCBData[2] = {};
        UINT sizes[2] = { (UINT)splitDataSize, (UINT)instObjectDataSize };
        bool res = GetDevice()->AllocateDynamicBuffers(instObjectDataSize == 0 ? 1 : 2, sizes, dynCBStartHandle, dynCBData, descs);
        assert(res);
        memcpy(dynCBData[0], pSplitData, splitDataSize);
        if (pInstObjectData != nullptr)
        {
            memcpy(dynCBData[1], pInstObjectData, instObjectDataSize);
        }
    }
    if (descs[0].BufferLocation != 0)
    {
        m_pCurrentRenderCommandList->SetGraphicsRootConstantBufferView(1, descs[0].BufferLocation);
    }
    if (descs[1].BufferLocation != 0)
    {
        m_pCurrentRenderCommandList->SetGraphicsRootConstantBufferView(2, descs[1].BufferLocation);
    }

    if (dynTexturesGpu.ptr != 0)
    {
        m_pCurrentRenderCommandList->SetGraphicsRootDescriptorTable(3, dynTexturesGpu);
    }

    m_pCurrentRenderCommandList->IASetVertexBuffers(0, 1, &geometry.vertexBufferView);
    m_pCurrentRenderCommandList->IASetIndexBuffer(&geometry.indexBufferView);

    m_pCurrentRenderCommandList->DrawIndexedInstanced(geometry.indexCount, 1, 0, 0, 0);
}

bool BaseRenderer::Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect)
{
    if (Platform::Renderer::Resize(viewport, rect))
    {
        UINT64 width = GetRect().right - GetRect().left;
        UINT64 height = GetRect().bottom - GetRect().top;

        assert(m_depthBuffer.pResource != nullptr);
        D3D12_RESOURCE_DESC desc = m_depthBuffer.pResource->GetDesc();
        if (desc.Width != width || desc.Height != height)
        {
            GetDevice()->ReleaseGPUResource(m_depthBuffer);

            return CreateDepthBuffer();
        }

        return true;
    }

    return false;
}

void BaseRenderer::SetupCurrentCommonBuffer()
{
    m_pCurrentRenderCommandList->SetGraphicsRootDescriptorTable(0, m_currentCommonTableStart);
}

bool BaseRenderer::RenderToCubemap(GPUResource& dst, GPUResource& rt, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv, const GeometryState& geomState, const D3D12_GPU_DESCRIPTOR_HANDLE& gpuTexHandle, int resPixels, int dstMip, int dstMipCount, int baseResource)
{
    static const std::vector<CubemapVertex> vertices{
        {Point3f{0.5,-0.5, 0.5}},
        {Point3f{0.5,-0.5,-0.5}},
        {Point3f{0.5, 0.5,-0.5}},
        {Point3f{0.5, 0.5, 0.5}}
    };
    static const std::vector<UINT16> indices{ 0,2,1,0,3,2 };

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDSVHandle();
    GetCurrentCommandList()->OMSetRenderTargets(1, &rtv, TRUE, &dsvHandle);

    int pixels = resPixels / (int)pow(2, dstMip);

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

    FLOAT clearColor[4] = { 0,0,0,0 };
    GetCurrentCommandList()->ClearRenderTargetView(rtv, clearColor, 1, &rect);
    GetCurrentCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

    UINT64 gpuVirtualAddress;

    CubemapVertex* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(vertices.size() * sizeof(CubemapVertex));
    bool res = GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        // Fill
        memcpy(pVertices, vertices.data(), sizeof(CubemapVertex) * vertices.size());

        // Setup
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        vertexBufferView.BufferLocation = gpuVirtualAddress;
        vertexBufferView.StrideInBytes = (UINT)sizeof(CubemapVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(indices.size() * sizeof(UINT16));
        res = GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
        if (res)
        {
            // Fill
            memcpy(pIndices, indices.data(), sizeof(UINT16) * indices.size());

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
        SetupGeometryState(geomState);
        GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, gpuTexHandle);

        res = GetDevice()->TransitResourceState(GetCurrentCommandList(), dst.pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    if (res)
    {
        for (int i = 0; i < 6 && res; i++)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC descs[1] = {};
            if (res)
            {
                D3D12_GPU_DESCRIPTOR_HANDLE dynCBStartHandle = {};

                UINT sizes[1] = { sizeof(Matrix4f) * 2 + sizeof(int) };
                UINT8* dynCBData[1] = {};
                res = GetDevice()->AllocateDynamicBuffers(1, sizes, dynCBStartHandle, dynCBData, descs);
                Matrix4f* pData = reinterpret_cast<Matrix4f*>(dynCBData[0]);

                pData[0] = m_cubemapFaceVP;
                pData[1] = m_faceTransforms[i];

                *(reinterpret_cast<float*>(&pData[2])) = (float)dstMip / (float)(dstMipCount - 1);
            }

            if (res)
            {
                GetCurrentCommandList()->SetGraphicsRootConstantBufferView(1, descs[0].BufferLocation);

                GetCurrentCommandList()->DrawIndexedInstanced((UINT)indices.size(), 1, 0, 0, 0);

                res = GetDevice()->TransitResourceState(GetCurrentCommandList(), rt.pResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                if (res)
                {
                    CD3DX12_TEXTURE_COPY_LOCATION dstLoc{ dst.pResource, (UINT)(i * dstMipCount + dstMip + baseResource) };
                    CD3DX12_TEXTURE_COPY_LOCATION srcLoc{ rt.pResource };

                    D3D12_BOX rect = {};
                    rect.front = 0;
                    rect.back = 1;
                    rect.right = rect.bottom = pixels;

                    GetCurrentCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &rect);
                }
                if (res)
                {
                    res = GetDevice()->TransitResourceState(GetCurrentCommandList(), rt.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
            }
        }
    }

    if (res)
    {
        res = GetDevice()->TransitResourceState(GetCurrentCommandList(), dst.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    return res;
}

bool BaseRenderer::CreateDepthBuffer()
{
    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    bool res = GetDevice()->CreateGPUResource(
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, GetRect().right - GetRect().left, GetRect().bottom - GetRect().top, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        m_depthBuffer
    );
    if (res)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
        dsvView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvView.Flags = D3D12_DSV_FLAG_NONE;

        GetDevice()->GetDXDevice()->CreateDepthStencilView(m_depthBuffer.pResource, &dsvView, m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());
    }

    return res;
}

bool BaseRenderer::CreateGeometryBuffers(const CreateGeometryParams& params, Geometry& geometry)
{
    bool res = true;

    // Create vertex and index buffers
    if (res)
    {
        // Use D3D12_RESOURCE_STATE_COMMON here as buffers always act like simultaneous resources and are subject of implicit state promotion
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(params.vertexDataSize), D3D12_RESOURCE_STATE_COMMON, nullptr, geometry.vertexBuffer, params.pVertices, params.vertexDataSize);
    }
    if (res)
    {
        geometry.vertexBufferView.BufferLocation = geometry.vertexBuffer.pResource->GetGPUVirtualAddress();
        geometry.vertexBufferView.StrideInBytes = (UINT)params.vertexDataStride;
        geometry.vertexBufferView.SizeInBytes = (UINT)(params.vertexDataSize);
    }
    if (res)
    {
        // Use D3D12_RESOURCE_STATE_COMMON here as buffers always act like simultaneous resources and are subject of implicit state promotion
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(params.indexDataSize), D3D12_RESOURCE_STATE_COMMON, nullptr, geometry.indexBuffer, params.pIndices, params.indexDataSize);
    }
    if (res)
    {
        geometry.indexBufferView.BufferLocation = geometry.indexBuffer.pResource->GetGPUVirtualAddress();
        geometry.indexBufferView.Format = params.indexFormat;
        geometry.indexBufferView.SizeInBytes = (UINT)(params.indexDataSize);
    }

    // Create views for static textures
    if (res && !params.geomStaticTextures.empty())
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuTextureHandle;
        res = GetDevice()->AllocateStaticDescriptors((UINT)params.geomStaticTextures.size(), cpuTextureHandle, gpuTextureHandle);
        if (res)
        {
            geometry.texturesTableStart = gpuTextureHandle;

            for (int i = 0; i < params.geomStaticTextures.size(); i++)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = params.geomStaticTextures[i].pResource->GetDesc().Format;
                if (texDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
                {
                    texDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                }
                UINT count = params.geomStaticTextures[i].pResource->GetDesc().DepthOrArraySize;
                texDesc.ViewDimension = params.geomStaticTextures[i].dimension;
                switch (texDesc.ViewDimension)
                {
                    case D3D12_SRV_DIMENSION_TEXTURE1D:
                        texDesc.Texture1D.MipLevels = params.geomStaticTextures[i].pResource->GetDesc().MipLevels;
                        break;

                    case D3D12_SRV_DIMENSION_TEXTURE2D:
                        if (count > 1)
                        {
                            texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                            texDesc.Texture2DArray.ArraySize = count;
                            texDesc.Texture2DArray.MipLevels = params.geomStaticTextures[i].pResource->GetDesc().MipLevels;
                        }
                        else
                        {
                            texDesc.Texture2D.MipLevels = params.geomStaticTextures[i].pResource->GetDesc().MipLevels;
                        }
                        break;

                    case D3D12_SRV_DIMENSION_TEXTURECUBE:
                        texDesc.TextureCube.MipLevels = params.geomStaticTextures[i].pResource->GetDesc().MipLevels;
                        break;

                    default:
                        assert(!"Unknown SRV type");
                        break;
                }
                GetDevice()->GetDXDevice()->CreateShaderResourceView(params.geomStaticTextures[i].pResource, &texDesc, cpuTextureHandle);

                cpuTextureHandle.ptr += GetDevice()->GetDXDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
    }

    // Finalize
    if (res)
    {
        UINT indexByteStride = 0;
        switch (params.indexFormat)
        {
            case DXGI_FORMAT_R16_UINT:
                indexByteStride = 2;
                break;

            case DXGI_FORMAT_R32_UINT:
                indexByteStride = 4;
                break;

            default:
                assert(0); // Unknown index format
                break;
        }

        geometry.indexCount = (UINT)params.indexDataSize / indexByteStride;
    }

    return res;
}

} // Platform
