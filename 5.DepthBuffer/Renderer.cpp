#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"

#include "D3D12MemAlloc.h"

#include <chrono>
#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::Renderer(pDevice)
    , CameraControlEuler()
    , m_pRootSignature(nullptr)
    , m_pVertexShader(nullptr)
    , m_pPixelShader(nullptr)
    , m_depthBuffer()
    , m_pDSVHeap(nullptr)
    , m_pCurrentRootSignature(nullptr)
{
}

Renderer::~Renderer()
{
    assert(m_pVertexShader == nullptr);
    assert(m_pPixelShader == nullptr);

    assert(m_pDSVHeap == nullptr);
}

bool Renderer::Init(HWND hWnd)
{
    static const Vertex vertices[8] = {
        {{-0.5f, -0.5f, -0.5f}, {0, 0, 1}},
        {{ 0.5f, -0.5f, -0.5f}, {0, 1, 0}},
        {{ 0.5f,  0.5f, -0.5f}, {0, 1, 1}},
        {{-0.5f,  0.5f, -0.5f}, {1, 0, 0}},
        {{-0.5f, -0.5f,  0.5f}, {1, 0, 1}},
        {{ 0.5f, -0.5f,  0.5f}, {1, 1, 0}},
        {{ 0.5f,  0.5f,  0.5f}, {1, 1, 1}},
        {{-0.5f,  0.5f,  0.5f}, {0, 0, 0}}
    };

    static const UINT16 indices[36] = {
        0, 2, 1, 0, 3, 2,
        4, 1, 5, 4, 0, 1,
        1, 6, 5, 1, 2, 6,
        5, 7, 4, 5, 6, 7,
        4, 3, 0, 4, 7, 3,
        3, 6, 2, 3, 7, 6
    };

    static const int GridCells = 10;
    static const float GridStep = 1.0f;

    std::vector<Vertex> gridVertices((GridCells + 1) * 4);
    std::vector<UINT16> gridIndices((GridCells + 1) * 4);
    for (int i = 0; i <= GridCells; i++)
    {
        gridVertices[i * 2 + 0].pos = Point3f{ (-GridCells / 2 + i) * GridStep, 0.0f, -GridCells / 2 * GridStep };
        gridVertices[i * 2 + 1].pos = Point3f{ (-GridCells / 2 + i) * GridStep, 0.0f,  GridCells / 2 * GridStep };
        gridVertices[i * 2 + 0].color = Point3f{ 1,1,1 };
        gridVertices[i * 2 + 1].color = Point3f{ 1,1,1 };

        gridVertices[(GridCells + 1) * 2 + i * 2 + 0].pos = Point3f{ -GridCells / 2 * GridStep, 0.0f, (-GridCells / 2 + i) * GridStep };
        gridVertices[(GridCells + 1) * 2 + i * 2 + 1].pos = Point3f{  GridCells / 2 * GridStep, 0.0f, (-GridCells / 2 + i) * GridStep };
        gridVertices[(GridCells + 1) * 2 + i * 2 + 0].color = Point3f{ 1,1,1 };
        gridVertices[(GridCells + 1) * 2 + i * 2 + 1].color = Point3f{ 1,1,1 };
    }
    for (int i = 0; i < gridIndices.size(); i++)
    {
        gridIndices[i] = (UINT16)i;
    }

    bool res = Platform::Renderer::Init(hWnd);
    if (res)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.NumDescriptors = 1;
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
        res = BeginGeometryCreation();
        if (res)
        {
            Geometry geometry;
            res = CreateGeometry(vertices, sizeof(vertices) / sizeof(Vertex), indices, sizeof(indices) / sizeof(UINT16), D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, geometry);
            if (res)
            {
                m_geometries.push_back(geometry);
            }
            if (res)
            {
                res = CreateGeometry(gridVertices.data(), gridVertices.size(), gridIndices.data(), gridIndices.size(), D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, geometry);
            }
            if (res)
            {
                m_geometries.push_back(geometry);
            }

            EndGeometryCreation();
        }
    }

    return res;
}

void Renderer::Term()
{
    for (auto geometry : m_geometries)
    {
        DestroyGeometry(geometry);
    }
    m_geometries.clear();

    GetDevice()->ReleaseGPUResource(m_depthBuffer);

    D3D_RELEASE(m_pRootSignature);
    D3D_RELEASE(m_pDSVHeap);

    Platform::Renderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 2 * deltaSec;

    UpdateCamera(deltaSec);

    Matrix4f rotation;
    rotation.Rotation((float)m_angle, Point3f{ 0, 1, 0 });

    m_geometries[0].trans = rotation;

    return true;
}

bool Renderer::Render()
{
    D3D12_RECT rect = GetRect();
    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    static const UINT sizes[1] = { sizeof(Matrix4f) };
    m_currentCommonTableStart = D3D12_GPU_DESCRIPTOR_HANDLE{};
    UINT8* dynCBData[1] = {};
    GetDevice()->AllocateDynamicBuffers(1, sizes, m_currentCommonTableStart, dynCBData);

    Matrix4f vp = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);

    memcpy(dynCBData[0], &vp, sizeof(vp));

    ID3D12GraphicsCommandList* pCommandList = nullptr;
    ID3D12Resource* pBackBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    if (GetDevice()->BeginRenderCommandList(&pCommandList, &pBackBuffer, &rtvHandle))
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

        HRESULT hr = S_OK;
        pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);

        D3D12_VIEWPORT viewport = GetViewport();
        pCommandList->RSSetViewports(1, &viewport);
        pCommandList->RSSetScissorRects(1, &rect);

        ID3D12DescriptorHeap* pHeap = GetDevice()->GetDescriptorHeap();
        pCommandList->SetDescriptorHeaps(1, &pHeap);

        m_pCurrentRootSignature = nullptr;

        if (GetDevice()->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            FLOAT clearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
            pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &rect);
            pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 1, &rect);

            for (const auto& geometry : m_geometries)
            {
                RenderGeometry(pCommandList, geometry);
            }

            GetDevice()->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        }

        GetDevice()->CloseSubmitAndPresentRenderCommandList();
    }

    return true;
}

bool Renderer::Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect)
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

bool Renderer::BeginGeometryCreation()
{
    bool res = true;
    if (res)
    {
        res = GetDevice()->CompileShader(_T("SimpleColor.hlsl"), {}, Platform::Device::Vertex, &m_pVertexShader);
    }
    if (res)
    {
        res = GetDevice()->CompileShader(_T("SimpleColor.hlsl"), {}, Platform::Device::Pixel, &m_pPixelShader);
    }
    // Create root signature
    if (res)
    {
        D3D12_DESCRIPTOR_RANGE descRanges[1] = {};
        descRanges[0].BaseShaderRegister = 0;
        descRanges[0].NumDescriptors = 1;
        descRanges[0].OffsetInDescriptorsFromTableStart = 0;
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRanges[0].RegisterSpace = 0;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = descRanges;

        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].Descriptor.ShaderRegister = 1;

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &m_pRootSignature);
    }
    if (res)
    {
        ID3D12GraphicsCommandList* pUploadCommandList = nullptr;
        res = GetDevice()->BeginUploadCommandList(&pUploadCommandList);
    }

    return res;
}

bool Renderer::CreateGeometry(const Vertex* pVertex, size_t vertexCount, const UINT16* pIndices, size_t indexCount, const D3D12_PRIMITIVE_TOPOLOGY_TYPE& primTopologyType, Geometry& geometry)
{
    bool res = true;
    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ vertexCount * sizeof(Vertex) }), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, geometry.vertexBuffer, pVertex, vertexCount * sizeof(Vertex));
    }
    if (res)
    {
        geometry.vertexBufferView.BufferLocation = geometry.vertexBuffer.pResource->GetGPUVirtualAddress();
        geometry.vertexBufferView.StrideInBytes = sizeof(Vertex);
        geometry.vertexBufferView.SizeInBytes = (UINT)(vertexCount * sizeof(Vertex));
    }
    // Create index buffer
    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ indexCount * sizeof(UINT16) }), D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr, geometry.indexBuffer, pIndices, indexCount * sizeof(UINT16));
    }
    if (res)
    {
        geometry.indexBufferView.BufferLocation = geometry.indexBuffer.pResource->GetGPUVirtualAddress();
        geometry.indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        geometry.indexBufferView.SizeInBytes = (UINT)(indexCount * sizeof(UINT16));
    }
    // Create PSO
    if (res)
    {
        static const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, 2 };
        psoDesc.pRootSignature = m_pRootSignature;
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_pVertexShader);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pPixelShader);
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = primTopologyType;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        res = GetDevice()->CreatePSO(psoDesc, &geometry.pPSO);
    }
    if (res)
    {
        geometry.indexCount = (UINT)indexCount;
        geometry.primType = primTopologyType;
        geometry.trans.Identity();
    }

    return res;
}

void Renderer::DestroyGeometry(Geometry& geometry)
{
    D3D_RELEASE(geometry.pPSO);

    GetDevice()->ReleaseGPUResource(geometry.vertexBuffer);
    GetDevice()->ReleaseGPUResource(geometry.indexBuffer);
}

void Renderer::EndGeometryCreation()
{
    GetDevice()->CloseUploadCommandList();

    D3D_RELEASE(m_pVertexShader);
    D3D_RELEASE(m_pPixelShader);
}

HRESULT Renderer::RenderGeometry(ID3D12GraphicsCommandList* pCommandList, const Geometry& geometry)
{
    HRESULT hr = S_OK;

    pCommandList->SetPipelineState(geometry.pPSO);

    if (m_pCurrentRootSignature != m_pRootSignature)
    {
        pCommandList->SetGraphicsRootSignature(m_pRootSignature);
        pCommandList->SetGraphicsRootDescriptorTable(0, m_currentCommonTableStart);

        m_pCurrentRootSignature = m_pRootSignature;
    }

    static const UINT sizes[1] = { sizeof(Matrix4f) };
    D3D12_GPU_DESCRIPTOR_HANDLE dynCBStartHandle = {};
    D3D12_CONSTANT_BUFFER_VIEW_DESC descs[1] = {};
    UINT8* dynCBData[1] = {};
    GetDevice()->AllocateDynamicBuffers(1, sizes, dynCBStartHandle, dynCBData, descs);
    memcpy(dynCBData[0], &geometry.trans, sizeof(Matrix4f));
    pCommandList->SetGraphicsRootConstantBufferView(1, descs[0].BufferLocation);

    switch (geometry.primType)
    {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
            pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            break;

        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            break;

        default:
            assert(0); // Unknown topology type
            break;
    }

    pCommandList->IASetVertexBuffers(0, 1, &geometry.vertexBufferView);
    pCommandList->IASetIndexBuffer(&geometry.indexBufferView);

    pCommandList->DrawIndexedInstanced(geometry.indexCount, 1, 0, 0, 0);

    return hr;
}

bool Renderer::CreateDepthBuffer()
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
