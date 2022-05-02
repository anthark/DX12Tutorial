#pragma once

#include "PlatformDevice.h"

class Renderer
{
public:
    Renderer();

    bool Init(Platform::Device* pDevice);
    void Term();

    bool Render(D3D12_VIEWPORT viewport, D3D12_RECT rect);

    inline Platform::Device* GetDevice() const { return m_pDevice; }

private:
    Platform::Device* m_pDevice;

    Platform::GPUResource m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    Platform::GPUResource m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

    ID3D12RootSignature* m_pRootSignature;
    ID3D12PipelineState* m_pPSO;
};
