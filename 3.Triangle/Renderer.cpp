#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"

#include "D3D12MemAlloc.h"

namespace
{

#pragma pack(push, 1)
struct Point3f
{
    float x, y, z;
};

struct Vertex
{
    Point3f pos;
    Point3f color;
};
#pragma pack(pop)

}

Renderer::Renderer()
    : m_pDevice(nullptr)
    , m_pRootSignature(nullptr)
    , m_pPSO(nullptr)
{
}

bool Renderer::Init(Platform::Device* pDevice)
{
    m_pDevice = pDevice;

    static const Vertex vertices[3] = {
        {{-0.5f, -0.5f, 0}, {1,0,0}},
        {{ 0, 0.5f, 0}, {0,0,1}},
        {{ 0.5f, -0.5f, 0}, {0,1,0}}
    };

    static const UINT16 indices[3] = { 0, 1, 2 };

    // Create vertex buffer
    bool res = true;
    if (res)
    {
        ID3D12GraphicsCommandList* pUploadCommandList = nullptr;
        res = m_pDevice->BeginUploadCommandList(&pUploadCommandList);
    }
    if (res)
    {
        res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ sizeof(vertices) }), D3D12_RESOURCE_STATE_COMMON, nullptr, m_vertexBuffer, vertices, sizeof(vertices));
    }
    if (res)
    {
        m_vertexBufferView.BufferLocation = m_vertexBuffer.pResource->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = sizeof(vertices);
    }
    // Create index buffer
    if (res)
    {
        res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ sizeof(indices) }), D3D12_RESOURCE_STATE_COMMON, nullptr, m_indexBuffer, indices, sizeof(indices));
    }
    if (res)
    {
        m_indexBufferView.BufferLocation = m_indexBuffer.pResource->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_indexBufferView.SizeInBytes = sizeof(indices);
    }
    if (res)
    {
        m_pDevice->CloseUploadCommandList();
    }
    // Compile shaders
    ID3DBlob* pVertexShaderBinary = nullptr;
    ID3DBlob* pPixelShaderBinary = nullptr;
    if (res)
    {
        res = m_pDevice->CompileShader(_T("SimpleColor.hlsl"), {}, Platform::Device::Vertex, &pVertexShaderBinary);
    }
    if (res)
    {
        res = m_pDevice->CompileShader(_T("SimpleColor.hlsl"), {}, Platform::Device::Pixel, &pPixelShaderBinary);
    }
    // Create root signature
    if (res)
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        res = m_pDevice->CreateRootSignature(rootSignatureDesc, &m_pRootSignature);
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
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderBinary);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderBinary);
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        res = m_pDevice->CreatePSO(psoDesc, &m_pPSO);
    }

    D3D_RELEASE(pPixelShaderBinary);
    D3D_RELEASE(pVertexShaderBinary);

    return res;
}

void Renderer::Term()
{
    D3D_RELEASE(m_pPSO);
    D3D_RELEASE(m_pRootSignature);

    m_pDevice->ReleaseGPUResource(m_vertexBuffer);
    m_pDevice->ReleaseGPUResource(m_indexBuffer);

    m_pDevice = nullptr;
}

bool Renderer::Render(D3D12_VIEWPORT viewport, D3D12_RECT rect)
{
    ID3D12GraphicsCommandList* pCommandList = nullptr;
    ID3D12Resource* pBackBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    if (m_pDevice->BeginRenderCommandList(&pCommandList, &pBackBuffer, &rtvHandle))
    {
        HRESULT hr = S_OK;
        pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, nullptr);

        pCommandList->RSSetViewports(1, &viewport);
        pCommandList->RSSetScissorRects(1, &rect);

        pCommandList->SetPipelineState(m_pPSO);
        pCommandList->SetGraphicsRootSignature(m_pRootSignature);
        pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        pCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        pCommandList->IASetIndexBuffer(&m_indexBufferView);

        if (m_pDevice->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            FLOAT clearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
            pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &rect);

            pCommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);

            m_pDevice->TransitResourceState(pCommandList, pBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        }

        m_pDevice->CloseSubmitAndPresentRenderCommandList();
    }

    return true;
}