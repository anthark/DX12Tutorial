#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"

#include "D3D12MemAlloc.h"

#include <chrono>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

namespace
{

struct Vertex
{
    Point3f pos;
    Point3f color;
};

}

Renderer::Renderer()
    : m_pDevice(nullptr)
    , m_pRootSignature(nullptr)
    , m_pPSO(nullptr)
    , m_usec(0)
    , m_cameraMoveSpeed(1.0f) // Linear units in 1 second
    , m_cameraRotateSpeed((float)M_PI) // Radians in from-border-to-border mouse move
    , m_rbPressed(false)
    , m_prevRbX(0)
    , m_prevRbY(0)
    , m_forwardAccel(0.0f)
    , m_rightAccel(0.0f)
    , m_angleDeltaX(0.0f)
    , m_angleDeltaY(0.0f)
{
}

bool Renderer::Init(Platform::Device* pDevice)
{
    m_pDevice = pDevice;

    static const Vertex vertices[3] = {
        {{ -1, 0, 0}, {1,0,0}},
        {{ 0, 1, 0}, {0,0,1}},
        {{ 1, 0, 0}, {0,1,0}}
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
        res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ sizeof(vertices) }), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, m_vertexBuffer, vertices, sizeof(vertices));
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
        res = pDevice->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer({ sizeof(indices) }), D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr, m_indexBuffer, indices, sizeof(indices));
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
        D3D12_DESCRIPTOR_RANGE descRanges[1] = {};
        descRanges[0].BaseShaderRegister = 0;
        descRanges[0].NumDescriptors = 1;
        descRanges[0].OffsetInDescriptorsFromTableStart = 0;
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRanges[0].RegisterSpace = 0;

        D3D12_ROOT_PARAMETER params[1] = {};
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = descRanges;

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(1, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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

void Renderer::Update()
{
    size_t usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_usec == 0)
    {
        m_usec = usec; // Initial update
    }

    double deltaSec = (usec - m_usec) / 1000000.0;

    m_angle += M_PI * deltaSec;

    ApplyCameraMoveParams((float)deltaSec);

    m_usec = usec;
}

bool Renderer::Render(D3D12_VIEWPORT viewport, D3D12_RECT rect)
{
    Update();

    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    static const UINT sizes[1] = { sizeof(Matrix4f) };
    D3D12_GPU_DESCRIPTOR_HANDLE dynCBStartHandle = {};
    UINT8* dynCBData[1] = {};
    m_pDevice->AllocateDynamicBuffers(1, sizes, dynCBStartHandle, dynCBData);

    Matrix4f rotation;
    rotation.Rotation((float)m_angle, Point3f{ 0, 1, 0 });

    rotation = rotation * m_camera.CalcViewMatrix() * m_camera.CalcProjMatrix(aspectRatioHdivW);

    memcpy(dynCBData[0], &rotation, sizeof(rotation));

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

        ID3D12DescriptorHeap* pHeap = m_pDevice->GetDescriptorHeap();
        pCommandList->SetDescriptorHeaps(1, &pHeap);
        pCommandList->SetGraphicsRootDescriptorTable(0, dynCBStartHandle);

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

void Renderer::SetAccelerator(float forwardAccel, float rightAccel)
{
    m_forwardAccel = forwardAccel;
    m_rightAccel = rightAccel;
}

bool Renderer::OnRButtonPressed(int x, int y)
{
    m_rbPressed = true;
    m_prevRbX = x;
    m_prevRbY = y;

    return true;
}

bool Renderer::OnRButtonReleased(int x, int y)
{
    m_rbPressed = false;
    m_prevRbX = x;
    m_prevRbY = y;

    return true;
}

bool Renderer::OnMouseMove(int x, int y, int flags, const RECT& rect)
{
    if (m_rbPressed)
    {
        m_angleDeltaX += (float)(x - m_prevRbX) / (rect.right - rect.left);
        m_angleDeltaY += (float)(m_prevRbY - y) / (rect.bottom - rect.top);

        m_prevRbX = x;
        m_prevRbY = y;

        return true;
    }

    return false;
}

bool Renderer::OnMouseWheel(int zDelta)
{
    float distance = std::max(0.0f, m_camera.GetDistance() - zDelta / 120.0f);
    m_camera.SetDistance(distance);

    return true;
}

void Renderer::ApplyCameraMoveParams(float deltaSec)
{
    float lat = m_camera.GetLat() - m_angleDeltaY * m_cameraRotateSpeed;
    lat = std::min(std::max(lat, -(float)M_PI / 2), (float)M_PI / 2);
    m_camera.SetLat(lat);

    float lon = m_camera.GetLon() - m_angleDeltaX * m_cameraRotateSpeed;
    m_camera.SetLon(lon);

    m_angleDeltaX = m_angleDeltaY = 0.0f;

    Point3f right, up, dir;
    m_camera.CalcDirection(right, up, dir);

    Point3f forward;
    if (m_camera.GetLat() > (float)M_PI / 4 || m_camera.GetLat() < -(float)M_PI / 4)
    {
        forward = up;
    }
    else
    {
        forward = up;
    }
    forward.y = 0.0f;
    forward.normalize();

    Point3f cameraMoveDir = forward * m_forwardAccel + right * m_rightAccel;
    cameraMoveDir.normalize();

    if (cameraMoveDir.lengthSqr() > 0.00001f)
    {
        m_camera.SetLookAt(m_camera.GetLookAt() + cameraMoveDir * m_cameraMoveSpeed * deltaSec);
    }
}
