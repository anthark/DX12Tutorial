#pragma once

#include "PlatformDevice.h"
#include "PlatformRenderWindow.h"

namespace Platform
{

class ShaderCache;

class PLATFORM_API BaseRenderer : public Renderer
{
public:
    struct GeometryState
    {
        ID3D12PipelineState* pPSO = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        ID3D12RootSignature* pRootSignature = nullptr;
    };

    struct Geometry : GeometryState
    {
        virtual const void* GetObjCB(size_t& size) const { return nullptr; }

        Platform::GPUResource vertexBuffer = Platform::GPUResource();
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        Platform::GPUResource indexBuffer = Platform::GPUResource();
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

        UINT indexCount = 0;

        D3D12_GPU_DESCRIPTOR_HANDLE texturesTableStart = { 0 };
    };

    struct BeginRenderParams
    {
        // Input
        Point4f backColor = Point4f{0,0,0,0};

        // CB output
        UINT8** ppCPUData = nullptr;

        // Texture output
        D3D12_CPU_DESCRIPTOR_HANDLE cpuTextureHandles = D3D12_CPU_DESCRIPTOR_HANDLE{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuTextureHandles = D3D12_GPU_DESCRIPTOR_HANDLE{};
    };

    struct GeometryAttributeParams
    {
        LPCSTR SemanticName;
        UINT SemanticIndex;
        DXGI_FORMAT Format;
        UINT AlignedByteOffset;
    };

    struct GeometryStateParams
    {
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;

        LPCTSTR pShaderSourceName = _T("");
        std::vector<LPCSTR> shaderDefines;

        std::vector<GeometryAttributeParams> geomAttributes;

        UINT geomStaticTexturesCount = 0;
        UINT geomDynamicTexturesCount = 0;

        D3D12_BLEND_DESC blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        D3D12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC depthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL, FALSE };

        DXGI_FORMAT rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT dsFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

        DXGI_FORMAT rtFormat2 = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT rtFormat3 = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT rtFormat4 = DXGI_FORMAT_UNKNOWN;
    };

    struct TextureParam
    {
        ID3D12Resource* pResource;
        D3D12_SRV_DIMENSION dimension;

        TextureParam(ID3D12Resource* _pResorce, const D3D12_SRV_DIMENSION& _dimension = D3D12_SRV_DIMENSION_TEXTURE2D)
            : pResource(_pResorce)
            , dimension(_dimension)
        {}
    };

    struct CreateGeometryParams : GeometryStateParams
    {
        const void* pVertices = nullptr;
        UINT vertexDataSize = 0;
        UINT vertexDataStride = 0;

        const void* pIndices = nullptr;
        UINT indexDataSize = 0;
        DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;

        std::vector<TextureParam> geomStaticTextures;
    };

public:
    BaseRenderer(Device* pDevice, UINT commonCBCount = 0, UINT commonTexCount = 0, const std::vector<UINT>& commonCBSizes = std::vector<UINT>(), UINT additionalDSDescCount = 0);
    virtual ~BaseRenderer();

    virtual bool Init(HWND hWnd) override;
    virtual void Term() override;

    bool BeginRender(BeginRenderParams& params);
    bool RestartCommonResources(BeginRenderParams& params);
    void ResetRender();
    void EndRender(bool vsync = true);

    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferDSVHandle() const;
    void SetBackBufferRT() const;

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVStartHandle() const;
    Platform::GPUResource GetDepthBuffer() const { return m_depthBuffer; }

    ID3D12GraphicsCommandList* GetCurrentCommandList() const { return m_pCurrentRenderCommandList; }
    void SetupGeometryState(const GeometryState& geomState);
    bool CreateGeometryState(const GeometryStateParams& params, GeometryState& geomState);
    void DestroyGeometryState(GeometryState& geomState);

    bool BeginGeometryCreation();
    void EndGeometryCreation();

    bool RenderToCubemap(GPUResource& dst, GPUResource& rt, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv, const GeometryState& geomState, const D3D12_GPU_DESCRIPTOR_HANDLE& gpuTexHandle, int resPixels, int dstMip, int dstMipCount, int baseResource = 0);
    bool CreateGeometry(const CreateGeometryParams& params, Geometry& geometry);
    void DestroyGeometry(Geometry& geometry);

    virtual bool RenderScene(const Camera& camera);

protected:
    bool CreateGeometrySharedState(const GeometryState& srcState, const CreateGeometryParams& params, Geometry& geometry);

    void RenderGeometry(const Geometry& geometry, const void* pInstData = nullptr, size_t instDataSize = 0, const GeometryState* pState = nullptr, D3D12_GPU_DESCRIPTOR_HANDLE dynTexturesGpu = {}, const void* pInstObjectData = nullptr, size_t instObjectDataSize = 0);

    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

    void SetupCurrentCommonBuffer();

    inline ShaderCache* GetShaderCache() const { return m_pShaderCache; }

private:
    bool CreateDepthBuffer();
    bool CreateGeometryBuffers(const CreateGeometryParams& params, Geometry& geometry);

private:
    ID3D12RootSignature* m_pCurrentRootSignature;
    D3D12_GPU_DESCRIPTOR_HANDLE m_currentCommonTableStart;

    ID3D12GraphicsCommandList* m_pCurrentRenderCommandList;
    ID3D12Resource* m_pCurrentBackBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTVHandle;

    Platform::GPUResource m_depthBuffer;

    ID3D12DescriptorHeap* m_pDSVHeap;

    std::vector<Matrix4f> m_faceTransforms;
    Matrix4f m_cubemapFaceVP;

    ShaderCache* m_pShaderCache;

    UINT m_commonCBCount;
    UINT m_commonTexCount;
    std::vector<UINT> m_commonCBSizes;

    UINT m_additionalDSDescCount;
};

} // Platform
