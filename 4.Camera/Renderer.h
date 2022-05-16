#pragma once

#include "PlatformDevice.h"
#include "PlatformCamera.h"

class Renderer
{
public:
    Renderer();

    bool Init(Platform::Device* pDevice);
    void Term();

    void Update();
    bool Render(D3D12_VIEWPORT viewport, D3D12_RECT rect);

    void SetAccelerator(float forwardAccel, float rightAccel);

    bool OnRButtonPressed(int x, int y);
    bool OnRButtonReleased(int x, int y);
    bool OnMouseMove(int x, int y, int flags, const RECT& rect);
    bool OnMouseWheel(int zDelta);

    inline Platform::Device* GetDevice() const { return m_pDevice; }

private:
    void ApplyCameraMoveParams(float deltaSec);

private:
    Platform::Device* m_pDevice;

    Platform::GPUResource m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    Platform::GPUResource m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

    ID3D12RootSignature* m_pRootSignature;
    ID3D12PipelineState* m_pPSO;

    size_t m_usec; // Current update moment in microseconds since first update
    double m_angle; // Current rotation angle for model

    Platform::Camera m_camera;

    float m_cameraMoveSpeed;
    float m_cameraRotateSpeed;

    bool m_rbPressed;
    int m_prevRbX;
    int m_prevRbY;

    // Camera movement parameters
    float m_forwardAccel;
    float m_rightAccel;
    float m_angleDeltaX;
    float m_angleDeltaY;
};
