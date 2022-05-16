#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformUtil.h"

#include "D3D12MemAlloc.h"

#include <chrono>
#include <algorithm>

#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "png.h"
#include "PlatformIO.h"

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
};

size_t CalculateSizeWithMips(const png_image& image, UINT& mipCount)
{
    DWORD mips = 0;
    _BitScanForward(&mips, std::min(NearestPowerOf2(image.height), NearestPowerOf2(image.width)));

    mipCount = (UINT)mips + 1;
    mipCount -= 2; // Skip last two mips, as texture cannot be less than 4x4 pixels

    size_t height = image.height;
    size_t stride = PNG_IMAGE_ROW_STRIDE(image);

    size_t res = 0;
    for (UINT i = 0; i < mipCount; i++)
    {
        res += PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) * height * stride;
        height /= 2;
        stride /= 2;
    }

    return res;
}

void GenerateMips(void* pInitialData, const png_image& image, UINT mipsToGenerate)
{
    assert(image.format == PNG_FORMAT_RGBA);

    UINT8* pData = static_cast<UINT8*>(pInitialData);

    size_t height = image.height;
    size_t width = image.width;
    size_t stride = PNG_IMAGE_ROW_STRIDE(image);

    for (UINT i = 0; i < mipsToGenerate; i++)
    {
        const UINT8* pSrcData = pData;
        pData += PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) * height * stride;
        height /= 2;
        width /= 2;
        stride /= 2;

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                for (int i = 0; i < 4; i++)
                {
                    int accum = 0;

                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 0) + (x * 2 + 0) * 4) * PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 1) + (x * 2 + 0) * 4) * PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 0) + (x * 2 + 1) * 4) * PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) + i);
                    accum += *(pSrcData + ((stride * 2) * (y * 2 + 1) + (x * 2 + 1) * 4) * PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) + i);
                    accum /= 4;

                    pData[(stride * y + x * 4) * PNG_IMAGE_PIXEL_COMPONENT_SIZE(image.format) + i] = (UINT8)accum;
                }
            }
        }
    }
}

}

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::Renderer(pDevice)
    , CameraControlEuler()
    , m_depthBuffer()
    , m_pDSVHeap(nullptr)
    , m_pCurrentRootSignature(nullptr)
{
}

Renderer::~Renderer()
{
    assert(m_pDSVHeap == nullptr);
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
            res = CreateTexture(_T("../Common/Kitty.png"));
        }
        if (res)
        {
            Geometry geometry;
            CreateGeometryParams params;

            Platform::GetCubeDataSize(false, true, indexCount, vertexCount);
            cubeVertices.resize(vertexCount);
            indices.resize(indexCount);
            Platform::CreateCube(indices.data(), sizeof(TextureVertex), &cubeVertices[0].pos, nullptr, &cubeVertices[0].uv);

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
            params.indexDataSize = indices.size() * sizeof(UINT16);
            params.indexFormat = DXGI_FORMAT_R16_UINT;
            params.pIndices = indices.data();
            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("SimpleTexture.hlsl");
            params.pVertices = cubeVertices.data();
            params.vertexDataSize = cubeVertices.size() * sizeof(TextureVertex);
            params.vertexDataStride = sizeof(TextureVertex);

            params.geomStaticTextures.push_back(m_textureResource.pResource);

            res = CreateGeometry(params, geometry);
            if (res)
            {
                m_geometries.push_back(geometry);
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
                params.indexDataSize = indices.size() * sizeof(UINT16);
                params.indexFormat = DXGI_FORMAT_R16_UINT;
                params.pIndices = indices.data();
                params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                params.pShaderSourceName = _T("SimpleColor.hlsl");
                params.pVertices = gridVertices.data();
                params.vertexDataSize = gridVertices.size() * sizeof(ColorVertex);
                params.vertexDataStride = sizeof(ColorVertex);

                params.geomStaticTextures.clear();

                res = CreateGeometry(params, geometry);
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
    GetDevice()->ReleaseGPUResource(m_textureResource);

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
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
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
            //FLOAT clearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
            FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
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
        ID3D12GraphicsCommandList* pUploadCommandList = nullptr;
        res = GetDevice()->BeginUploadCommandList(&pUploadCommandList);
    }

    return res;
}

bool Renderer::CreateGeometry(const CreateGeometryParams& params, Geometry& geometry)
{
    bool res = true;

    // Create root signature
    if (res)
    {
        std::vector<D3D12_ROOT_PARAMETER> rootSignatureParams;

        D3D12_DESCRIPTOR_RANGE descRangesCommon[1] = {};
        descRangesCommon[0].BaseShaderRegister = 0;
        descRangesCommon[0].NumDescriptors = 1;
        descRangesCommon[0].OffsetInDescriptorsFromTableStart = 0;
        descRangesCommon[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descRangesCommon[0].RegisterSpace = 0;
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = descRangesCommon;

            rootSignatureParams.push_back(param);
        }
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.RegisterSpace = 0;
            param.Descriptor.ShaderRegister = 1;

            rootSignatureParams.push_back(param);
        }

        D3D12_DESCRIPTOR_RANGE descRangesGeometry[1] = {};
        descRangesGeometry[0].BaseShaderRegister = 0;
        descRangesGeometry[0].NumDescriptors = (UINT)params.geomStaticTextures.size();
        descRangesGeometry[0].OffsetInDescriptorsFromTableStart = 0;
        descRangesGeometry[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRangesGeometry[0].RegisterSpace = 0;
        if (!params.geomStaticTextures.empty())
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = descRangesGeometry;

            rootSignatureParams.push_back(param);
        }

        CD3DX12_STATIC_SAMPLER_DESC samplerDesc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init((UINT)rootSignatureParams.size(), rootSignatureParams.data(), 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        res = GetDevice()->CreateRootSignature(rootSignatureDesc, &geometry.pRootSignature);
    }

    // Create shaders
    ID3DBlob* pVertexShaderBinary = nullptr;
    ID3DBlob* pPixelShaderBinary = nullptr;
    if (res)
    {
        res = GetDevice()->CompileShader(params.pShaderSourceName, {}, Platform::Device::Vertex, &pVertexShaderBinary);
    }
    if (res)
    {
        res = GetDevice()->CompileShader(params.pShaderSourceName, {}, Platform::Device::Pixel, &pPixelShaderBinary);
    }

    // Create PSO
    if (res)
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
        for (const auto& attribute : params.geomAttributes)
        {
            D3D12_INPUT_ELEMENT_DESC elementDesc = {attribute.SemanticName, attribute.SemanticIndex, attribute.Format, 0, attribute.AlignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};

            inputElementDescs.push_back(elementDesc);
        }

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs.data(), (UINT)inputElementDescs.size() };
        psoDesc.pRootSignature = geometry.pRootSignature;
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderBinary);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderBinary);
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = params.primTopologyType;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        res = GetDevice()->CreatePSO(psoDesc, &geometry.pPSO);
    }

    // Create vertex and index buffers
    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(params.vertexDataSize), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, geometry.vertexBuffer, params.pVertices, params.vertexDataSize);
    }
    if (res)
    {
        geometry.vertexBufferView.BufferLocation = geometry.vertexBuffer.pResource->GetGPUVirtualAddress();
        geometry.vertexBufferView.StrideInBytes = (UINT)params.vertexDataStride;
        geometry.vertexBufferView.SizeInBytes = (UINT)(params.vertexDataSize);
    }
    if (res)
    {
        res = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Buffer(params.indexDataSize), D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr, geometry.indexBuffer, params.pIndices, params.indexDataSize);
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
                texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(params.geomStaticTextures[i], &texDesc, cpuTextureHandle);

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

            default:
                assert(0); // Unknown index format
                break;
        }

        geometry.indexCount = (UINT)params.indexDataSize / indexByteStride;
        geometry.primType = params.primTopologyType;
        geometry.trans.Identity();
    }

    D3D_RELEASE(pVertexShaderBinary);
    D3D_RELEASE(pPixelShaderBinary);

    return res;
}

void Renderer::DestroyGeometry(Geometry& geometry)
{
    D3D_RELEASE(geometry.pPSO);
    D3D_RELEASE(geometry.pRootSignature);

    GetDevice()->ReleaseGPUResource(geometry.vertexBuffer);
    GetDevice()->ReleaseGPUResource(geometry.indexBuffer);
}

void Renderer::EndGeometryCreation()
{
    GetDevice()->CloseUploadCommandList();
}

HRESULT Renderer::RenderGeometry(ID3D12GraphicsCommandList* pCommandList, const Geometry& geometry)
{
    HRESULT hr = S_OK;

    pCommandList->SetPipelineState(geometry.pPSO);

    if (m_pCurrentRootSignature != geometry.pRootSignature)
    {
        pCommandList->SetGraphicsRootSignature(geometry.pRootSignature);
        pCommandList->SetGraphicsRootDescriptorTable(0, m_currentCommonTableStart);
        if (geometry.texturesTableStart.ptr != 0)
        {
            pCommandList->SetGraphicsRootDescriptorTable(2, geometry.texturesTableStart);
        }

        m_pCurrentRootSignature = geometry.pRootSignature;
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

bool Renderer::CreateTexture(LPCTSTR filename)
{
    std::vector<char> data;
    if (Platform::ReadFileContent(filename, data))
    {
        png_image image;
        memset(&image, 0, sizeof(png_image));
        image.version = PNG_IMAGE_VERSION;

        int pngRes = png_image_begin_read_from_memory(&image, &data[0], data.size());
        assert(pngRes != 0);

        if (pngRes != 0)
        {
            UINT pitch = PNG_IMAGE_ROW_STRIDE(image);

            UINT mips = 0;
            size_t dataSize = CalculateSizeWithMips(image, mips);

            UINT8* pBuffer = new UINT8[dataSize];

            pngRes = png_image_finish_read(&image, NULL, pBuffer, 0, NULL);

            GenerateMips(pBuffer, image, mips - 1);

            HRESULT hr = GetDevice()->CreateGPUResource(CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height, 1, mips), D3D12_RESOURCE_STATE_COMMON, nullptr, m_textureResource, pBuffer, dataSize);
            if (SUCCEEDED(hr))
            {
                hr = m_textureResource.pResource->SetName(filename);
            }

            delete[] pBuffer;
            pBuffer = nullptr;

            return SUCCEEDED(hr);
        }
    }

    return false;
}
