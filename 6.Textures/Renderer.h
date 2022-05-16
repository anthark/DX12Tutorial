#pragma once

#include "PlatformDevice.h"
#include "PlatformRenderWindow.h"
#include "CameraControl/PlatformCameraControlEuler.h"

class Renderer : public Platform::Renderer, public Platform::CameraControlEuler
{
public:
    struct Geometry
    {
        Matrix4f trans = Matrix4f();

        Platform::GPUResource vertexBuffer = Platform::GPUResource();
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        Platform::GPUResource indexBuffer = Platform::GPUResource();
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

        ID3D12PipelineState* pPSO = nullptr;

        UINT indexCount = 0;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;

        ID3D12RootSignature* pRootSignature = nullptr;

        D3D12_GPU_DESCRIPTOR_HANDLE texturesTableStart = {0};
    };

public:
    Renderer(Platform::Device* pDevice);
    virtual ~Renderer();

    virtual bool Init(HWND hWnd) override;
    virtual void Term() override;

    virtual bool Update(double elapsedSec, double deltaSec) override;
    virtual bool Render() override;
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:

    struct GeometryAttributeParams
    {
        LPCSTR SemanticName;
        UINT SemanticIndex;
        DXGI_FORMAT Format;
        UINT AlignedByteOffset;
    };

    struct CreateGeometryParams
    {
        const void* pVertices = nullptr;
        size_t vertexDataSize = 0;
        size_t vertexDataStride = 0;

        const void* pIndices = nullptr;
        size_t indexDataSize = 0;
        DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;

        D3D12_PRIMITIVE_TOPOLOGY_TYPE primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;

        LPCTSTR pShaderSourceName = _T("");

        std::vector<GeometryAttributeParams> geomAttributes;
        std::vector<ID3D12Resource*> geomStaticTextures;
    };

private:
    bool BeginGeometryCreation();
    bool CreateGeometry(const CreateGeometryParams& params, Geometry& geometry);
    void DestroyGeometry(Geometry& geometry);
    void EndGeometryCreation();

    HRESULT RenderGeometry(ID3D12GraphicsCommandList* pCommandList, const Geometry& geometry);

    bool CreateDepthBuffer();
    bool CreateTexture(LPCTSTR filename);

private:
    std::vector<Geometry> m_geometries;

    Platform::GPUResource m_textureResource;

    Platform::GPUResource m_depthBuffer;

    ID3D12DescriptorHeap* m_pDSVHeap;

    ID3D12RootSignature* m_pCurrentRootSignature;
    D3D12_GPU_DESCRIPTOR_HANDLE m_currentCommonTableStart;

    double m_angle; // Current rotation angle for model
};
