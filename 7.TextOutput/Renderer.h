#pragma once

#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"
#include "CameraControl/PlatformCameraControlEuler.h"

#include "ft2build.h"
#include FT_FREETYPE_H

class Renderer : public Platform::BaseRenderer, public Platform::CameraControlEuler
{
public:
    Renderer(Platform::Device* pDevice);
    virtual ~Renderer();

    virtual bool Init(HWND hWnd) override;
    virtual void Term() override;

    virtual bool Update(double elapsedSec, double deltaSec) override;
    virtual bool Render() override;

private:

    struct SymbolData
    {
        Point2f leftTop;                ///< Left top corner in texture coordinates
        Point2f rightBottom;            ///< Right bottom corner in texture coordinates
        Point2f symbolSize;             ///< Symbol size in pixels
        int     advance;                ///< Horizontal advance to the next symbol in line
        Point2f base;                   ///< Base point (left-top position) for bitmap for this symbol, in pixels
    };

    struct Geometry : BaseRenderer::Geometry
    {
        virtual const void* GetObjCB(size_t& size) const override { size = sizeof(trans); return &trans; }

        Matrix4f trans;
    };

private:
    bool CreateFont(LPCTSTR fontFilename, int fontSize);
    void DrawText(LPCTSTR text, const Point3f& color);

private:
    std::vector<Geometry> m_geometries;

    Platform::GPUResource m_textureResource;
    Platform::GPUResource m_fontResource;

    double m_angle; // Current rotation angle for model

    // Font data
    FT_Library m_ftLibrary;
    std::vector<SymbolData> m_symbolsData;

    GeometryState m_textGeomState;
    D3D12_GPU_DESCRIPTOR_HANDLE m_fontTextureHandle;

    // Values for FPS counting
    int m_fpsCount;
    double m_prevFPS;

    double m_fps;
};
