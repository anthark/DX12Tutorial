#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "PlatformTextDraw.h"
#include "CameraControl/PlatformCameraControlEuler.h"

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

protected:
    virtual bool Resize(const D3D12_VIEWPORT& viewport, const D3D12_RECT& rect) override;

private:

    struct Geometry : BaseRenderer::Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(objCB); return &objCB; }

        struct
        {
            Matrix4f trans;
            Matrix4f transNormals;
        } objCB;
    };

private:
    void SetupLights(Lights* pLights);

private:
    std::vector<Geometry> m_geometries;

    Platform::GPUResource m_textureResource;
    Platform::GPUResource m_plasterTextureResource;

    double m_angle; // Current rotation angle for model

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;

    Platform::TextDraw* m_pTextDraw;
    Platform::TextDraw::FontId m_fontId;
};
