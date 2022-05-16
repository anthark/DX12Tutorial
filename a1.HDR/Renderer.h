#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include "Object.h"

struct Lights;

class Renderer : public Platform::BaseRenderer, public Platform::CameraControlEuler
{
public:
    Renderer(Platform::Device* pDevice);
    virtual ~Renderer();

    virtual bool Init(HWND hWnd) override;
    virtual void Term() override;

    virtual bool Update(double elapsedSec, double deltaSec) override;
    virtual bool Render() override;

    virtual bool OnKeyDown(int virtualKeyCode) override;

protected:
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:
    static const Point4f BackColor;
    static const DXGI_FORMAT HDRFormat;

private:

    struct Geometry : BaseRenderer::Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objData); return &objData; }

        ObjectData objData;
    };

private:
    void MeasureLuminance();
    void Tonemap();
    void SetupLights(Lights* pLights);

    bool CreateHDRTexture();
    void DestroyHDRTexture();

    bool CreateComputePipeline();
    void DestroyComputePipeline();

private:
    std::vector<Geometry> m_geometries;

    Platform::GPUResource m_textureResource;
    Platform::GPUResource m_plasterTextureResource;

    Platform::GPUResource m_hdrRT;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hdrSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hdrSRVCpu;

    Platform::GPUResource m_lumTexture0;
    Platform::GPUResource m_lumTexture1;
    Point2i m_lum0Groups;
    Point2i m_lum1Groups;
    ID3D12PipelineState* m_pLuminancePSO;
    ID3D12RootSignature* m_pLuminanceRS;
    ID3D12PipelineState* m_pLuminanceFinalPSO;
    ID3D12RootSignature* m_pLuminanceFinalRS;
    Platform::GPUResource m_tonemapParams;

    GeometryState m_tonemapGeomState;

    double m_angle; // Current rotation angle for model

    float m_lastUpdateDelta;
    int m_tonemapMode;

    float m_brightLightBrightness;

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;

    Platform::TextDraw* m_pTextDraw;
    Platform::TextDraw::FontId m_fontId;
};
