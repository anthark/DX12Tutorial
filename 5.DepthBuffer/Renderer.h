#pragma once

#include "PlatformDevice.h"
#include "PlatformRenderWindow.h"
#include "CameraControl/PlatformCameraControlEuler.h"

class Renderer : public Platform::Renderer, public Platform::CameraControlEuler
{
public:
    struct Vertex
    {
        Point3f pos;
        Point3f color;
    };

    struct Geometry
    {
        Matrix4f trans;

        Platform::GPUResource vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        Platform::GPUResource indexBuffer;
        D3D12_INDEX_BUFFER_VIEW indexBufferView;

        ID3D12PipelineState* pPSO;

        UINT indexCount = 0;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primType;
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
    bool BeginGeometryCreation();
    bool CreateGeometry(const Vertex* pVertex, size_t vertexCount, const UINT16* pIndices, size_t indexCount, const D3D12_PRIMITIVE_TOPOLOGY_TYPE& primTopologyType, Geometry& geometry);
    void DestroyGeometry(Geometry& geometry);
    void EndGeometryCreation();

    HRESULT RenderGeometry(ID3D12GraphicsCommandList* pCommandList, const Geometry& geometry);

    bool CreateDepthBuffer();

private:
    ID3DBlob* m_pVertexShader;
    ID3DBlob* m_pPixelShader;

    std::vector<Geometry> m_geometries;

    Platform::GPUResource m_depthBuffer;

    ID3D12RootSignature* m_pRootSignature;
    ID3D12DescriptorHeap* m_pDSVHeap;

    ID3D12RootSignature* m_pCurrentRootSignature;
    D3D12_GPU_DESCRIPTOR_HANDLE m_currentCommonTableStart;

    double m_angle; // Current rotation angle for model
};
