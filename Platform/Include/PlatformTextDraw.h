#pragma once

#include "ft2build.h"
#include FT_FREETYPE_H

#include "PlatformPoint.h"
#include "PlatformDevice.h"
#include "PlatformBaseRenderer.h"

namespace Platform
{

class PLATFORM_API TextDraw
{
public:
    using FontId = int;

public:

    TextDraw();
    virtual ~TextDraw();

    bool Init(BaseRenderer* pRenderer);
    void Term();

    bool CreateFont(LPCTSTR fontFilename, int fontSize, FontId& fontId);

    void ResetCaret();
    void DrawText(const FontId& fontId, const Point3f& color, LPCTSTR format, ...);

    void Resize(const RECT& rect);

private:

    struct SymbolData
    {
        Point2f leftTop;                ///< Left top corner in texture coordinates
        Point2f rightBottom;            ///< Right bottom corner in texture coordinates
        Point2f symbolSize;             ///< Symbol size in pixels
        int     advance;                ///< Horizontal advance to the next symbol in line
        Point2f base;                   ///< Base point (left-top position) for bitmap for this symbol, in pixels
    };

    struct Font
    {
        std::vector<SymbolData> symbolsData;
        Platform::GPUResource texture;
        D3D12_GPU_DESCRIPTOR_HANDLE fontTextureHandle;
    };

private:
    void DrawText_Internal(const FontId& fontId, const Point3f& color, LPCTSTR text, const Point2i& offset = Point2i{0,0}, bool advanceY = true);
    float CalcStringHeight(const FontId& fontId, LPCTSTR text);

private:
    FT_Library m_ftLibrary;

    BaseRenderer* m_pRenderer;

    std::vector<Font> m_fonts;

    RECT m_rect;

    float m_textStartY;

    BaseRenderer::GeometryState m_textGeomState;
};

} // Platform
