#include "stdafx.h"
#include "PlatformTextDraw.h"

#include "PlatformIO.h"
#include "PlatformUtil.h"
#include "PlatformTexture.h"

#include <algorithm>
#include <stdarg.h>

namespace
{

struct TextVertex
{
    Point3f pos;
    Point3f color;
    Point2f uv;
};

}

namespace Platform
{

TextDraw::TextDraw()
    : m_ftLibrary(nullptr)
    , m_pRenderer(nullptr)
    , m_rect()
    , m_textStartY(0)
{

}

TextDraw::~TextDraw()
{
    assert(m_fonts.empty());
    assert(m_pRenderer == nullptr);
    assert(m_ftLibrary == nullptr);
}

bool TextDraw::Init(BaseRenderer* pRenderer)
{
    FT_Error error = FT_Init_FreeType(&m_ftLibrary);

    if (error == 0)
    {
        m_pRenderer = pRenderer;
    }

    if (error == 0)
    {
        BaseRenderer::GeometryStateParams geomStateParams;
        geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
        geomStateParams.geomAttributes.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
        geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 24 });
        geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        geomStateParams.pShaderSourceName = _T("../Common/Shaders/Text.hlsl");
        geomStateParams.geomStaticTexturesCount = 1;
        geomStateParams.depthStencilState.DepthEnable = FALSE;
        geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;

        geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
        geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

        error = m_pRenderer->CreateGeometryState(geomStateParams, m_textGeomState) ? 0 : 1;
    }

    assert(error == 0);

    return error == 0;
}

void TextDraw::Term()
{
    for (auto font : m_fonts)
    {
        m_pRenderer->GetDevice()->ReleaseGPUResource(font.texture);
    }
    m_fonts.clear();

    m_pRenderer->DestroyGeometryState(m_textGeomState);

    m_pRenderer = nullptr;

    if (m_ftLibrary != nullptr)
    {
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
    }
}

bool TextDraw::CreateFont(LPCTSTR fontFilename, int fontSize, FontId& fontId)
{
    std::vector<char> data;
    if (!Platform::ReadFileContent(fontFilename, data))
    {
        return false;
    }

    // Create FreeType typeface
    FT_Face face = 0;
    FT_Error error = 0;
    if (error == 0)
    {
        error = FT_New_Memory_Face(m_ftLibrary, (const FT_Byte*)data.data(), (FT_Long)data.size(), 0, &face);
    }
    if (error == 0)
    {
        error = FT_Set_Pixel_Sizes(face, (FT_UInt)fontSize, (FT_UInt)fontSize);
    }
    // Calculate maximum symbol height and width
    unsigned int symbolCount = 0;
    unsigned int symbolWidth = 0;
    unsigned int symbolHeight = 0;
    if (error == 0)
    {
        FT_GlyphSlot slot = face->glyph;

        for (int i = 0x20; i <= 0x7f && error == 0; i++) // ASCII symbols
        {
            ++symbolCount;

            FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)i);

            error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
            if (error == 0)
            {
                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            }
            if (error == 0)
            {
                // Recalculate maximum size
                if (slot->bitmap.width > symbolWidth)
                {
                    symbolWidth = slot->bitmap.width;
                }
                if (slot->bitmap.rows > symbolHeight)
                {
                    symbolHeight = slot->bitmap.rows;
                }
            }
        }
    }

    // Calculate texture size for font
    UINT textureWidth = 0;
    UINT textureHeight = 0;
    UINT horSymbols = 0;
    UINT vertSymbols = 0;
    if (error == 0)
    {
        // Calculate texture size
        if (error == 0)
        {
            int symbolSquare = symbolWidth * symbolHeight;
            int textureSquare = symbolSquare * symbolCount;

            textureWidth = (int)ceil(sqrtf((float)textureSquare));
            textureWidth = NearestPowerOf2(textureWidth);

            horSymbols = textureWidth / symbolWidth;
            vertSymbols = DivUp((UINT)symbolCount, horSymbols);

            textureHeight = NearestPowerOf2(vertSymbols * symbolHeight);
        }
    }

    Font newFont;

    // Render glyphs
    UINT8* pHostBuffer = nullptr;
    if (error == 0)
    {
        newFont.symbolsData.resize(symbolCount);

        pHostBuffer = new UINT8[textureWidth * textureHeight];
        memset(pHostBuffer, 0, textureWidth * textureHeight);

        // Cycle over symbols
        for (UINT ch = 0x20; ch <= 0x7f && error == 0; ch++)
        {
            FT_UInt glyphIndex = FT_Get_Char_Index(face, ch);

            error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
            if (error == 0)
            {
                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            }

            if (error == 0)
            {
                UINT idx = ch - 0x20;

                int symbolX = idx % horSymbols * symbolWidth;
                int symbolY = idx / horSymbols * symbolHeight;

                FT_GlyphSlot slot = face->glyph;

                // Cycle over symbol pixels
                for (UINT j = 0; j < slot->bitmap.rows; j++)
                {
                    for (UINT i = 0; i < slot->bitmap.width; i++)
                    {
                        // Destination pixel coords
                        int dstX = symbolX + i;
                        int dstY = symbolY + j;

                        switch (slot->bitmap.pixel_mode)
                        {
                        case FT_PIXEL_MODE_MONO:
                        {
                            int byteIdx = i / 8;
                            int bitIdx = 7 - (i % 8);
                            pHostBuffer[dstY * textureWidth + dstX] = ((slot->bitmap.buffer[j * slot->bitmap.pitch + byteIdx] >> bitIdx) & 0x1) * 0xff;
                        }
                        break;

                        case FT_PIXEL_MODE_GRAY:
                            pHostBuffer[dstY * textureWidth + dstX] = slot->bitmap.buffer[j * slot->bitmap.pitch + i];
                            break;

                        default:
                            assert(0); // FT: Unknown pixel mode
                            break;
                        }
                    }
                } // Cycle over symbol's pixels

                newFont.symbolsData[idx].leftTop = Point2f{ (float)symbolX / textureWidth, (float)symbolY / textureHeight };
                newFont.symbolsData[idx].rightBottom = Point2f{ (float)(symbolX + slot->bitmap.width) / textureWidth, (float)(symbolY + slot->bitmap.rows) / textureHeight };
                newFont.symbolsData[idx].symbolSize = Point2f{ (float)slot->bitmap.width, (float)slot->bitmap.rows };
                newFont.symbolsData[idx].advance = slot->advance.x >> 6;
                newFont.symbolsData[idx].base = Point2f{ (float)slot->bitmap_left, (float)slot->bitmap_top };
            }
        } // Cycle over symbols
    }

    // Create texture
    if (error == 0 && pHostBuffer != nullptr)
    {
        Platform::CreateTextureParams params = { textureWidth, textureHeight, DXGI_FORMAT_A8_UNORM };

        error = Platform::CreateTexture(params, false, m_pRenderer->GetDevice(), newFont.texture, pHostBuffer, textureHeight * textureWidth) ? 0 : 1;

        delete[] pHostBuffer;
        pHostBuffer = nullptr;
    }

    if (error == 0 && newFont.texture.pResource != nullptr)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;
        bool res = m_pRenderer->GetDevice()->AllocateStaticDescriptors(1, cpuTextureHandle, newFont.fontTextureHandle);
        if (res)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
            texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            texDesc.Format = newFont.texture.pResource->GetDesc().Format;
            texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            texDesc.Texture2D.MipLevels = 1;
            m_pRenderer->GetDevice()->GetDXDevice()->CreateShaderResourceView(newFont.texture.pResource, &texDesc, cpuTextureHandle);
        }
    }

    assert(error == 0);

    if (error == 0)
    {
        m_fonts.push_back(newFont);
        fontId = (FontId)m_fonts.size() - 1;
    }

    return error == 0;
}

void TextDraw::ResetCaret()
{
    m_textStartY = 0;
}

void TextDraw::DrawText_Internal(const FontId& fontId, const Point3f& color, LPCTSTR text, const Point2i& offset, bool advanceY)
{
    assert(fontId >= 0 && fontId < (FontId)m_fonts.size());

    UINT count = (UINT)_tcslen(text);

    UINT64 gpuVirtualAddress;

    TextVertex* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(count * 4 * sizeof(TextVertex));
    bool res = m_pRenderer->GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        // Calculate Y bearing
        float bearing = 0;
        for (UINT i = 0; i < count; i++)
        {
            const SymbolData& data = m_fonts[fontId].symbolsData[text[i] - 0x20];
            bearing = std::max(bearing, data.base.y);
        }

        // Fill letter's data
        float divX = 2.0f / (m_rect.right - m_rect.left);
        float divY = 2.0f / (m_rect.bottom - m_rect.top);

        Point3f localPos{ 0 + (float)offset.x, m_textStartY + bearing + (float)offset.y, 0 };
        for (UINT i = 0; i < count; i++)
        {
            const SymbolData& data = m_fonts[fontId].symbolsData[text[i] - 0x20];

            Point3f base{ data.base.x, -data.base.y, 0.0f };

            pVertices[i * 4 + 0].pos = localPos + base;
            pVertices[i * 4 + 1].pos = Point3f{ localPos.x + data.symbolSize.x, localPos.y, 0 } +base;
            pVertices[i * 4 + 2].pos = Point3f{ localPos.x + data.symbolSize.x, localPos.y + data.symbolSize.y, 0 } +base;
            pVertices[i * 4 + 3].pos = Point3f{ localPos.x, localPos.y + data.symbolSize.y, 0 } +base;

            pVertices[i * 4 + 0].uv = data.leftTop;
            pVertices[i * 4 + 1].uv = Point2f{ data.rightBottom.x, data.leftTop.y };
            pVertices[i * 4 + 2].uv = data.rightBottom;
            pVertices[i * 4 + 3].uv = Point2f{ data.leftTop.x, data.rightBottom.y };

            // Recalculate to NDC, setup color
            for (UINT j = 0; j < 4; j++)
            {
                pVertices[i * 4 + j].color = color;

                pVertices[i * 4 + j].pos.x = pVertices[i * 4 + j].pos.x * divX - 1.0f;
                pVertices[i * 4 + j].pos.y = -(pVertices[i * 4 + j].pos.y * divY - 1.0f);
            }

            localPos.x += data.advance;
        }

        // Setup
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        vertexBufferView.BufferLocation = gpuVirtualAddress;
        vertexBufferView.StrideInBytes = (UINT)sizeof(TextVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        m_pRenderer->GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(count * 6 * sizeof(UINT16));
        res = m_pRenderer->GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
        if (res)
        {
            // Fill
            for (UINT i = 0; i < count; i++)
            {
                pIndices[i * 6 + 0] = i * 4 + 0;
                pIndices[i * 6 + 1] = i * 4 + 1;
                pIndices[i * 6 + 2] = i * 4 + 2;
                pIndices[i * 6 + 3] = i * 4 + 0;
                pIndices[i * 6 + 4] = i * 4 + 2;
                pIndices[i * 6 + 5] = i * 4 + 3;
            }

            // Setup
            D3D12_INDEX_BUFFER_VIEW indexBufferView;

            indexBufferView.BufferLocation = gpuVirtualAddress;
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            m_pRenderer->GetCurrentCommandList()->IASetIndexBuffer(&indexBufferView);
        }
    }

    if (res)
    {
        m_pRenderer->SetupGeometryState(m_textGeomState);

        m_pRenderer->GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, m_fonts[fontId].fontTextureHandle);

        m_pRenderer->GetCurrentCommandList()->DrawIndexedInstanced(count * 6, 1, 0, 0, 0);
    }

    if (advanceY)
    {
        m_textStartY += CalcStringHeight(fontId, text);
    }
}

void TextDraw::DrawText(const FontId& fontId, const Point3f& color, LPCTSTR format, ...)
{
    va_list args;
    va_start(args, format);

    TCHAR buffer[4096 + 1];
    _vstprintf(buffer, 4096, format, args);

    va_end(args);

    DrawText_Internal(fontId, Point3f{ 0,0,0 }, buffer, Point2i{ 1,1 }, false);
    DrawText_Internal(fontId, color, buffer);
}

void TextDraw::Resize(const RECT& rect)
{
    m_rect = rect;
}

float TextDraw::CalcStringHeight(const FontId& fontId, LPCTSTR text)
{
    float max = std::numeric_limits<float>::lowest();
    float min = std::numeric_limits<float>::max();

    size_t i = 0;
    for (i = 0; i < _tcslen(text); i++)
    {
        const SymbolData& data = m_fonts[fontId].symbolsData[text[i] - 0x20];
        max = std::max(max, (float)data.base.y); // bearing Y
        min = std::min(min, (float)data.base.y - data.symbolSize.y); // lowest pixel under base line
    }
    return min < max ? max - min : 0.0f;
}

} // Platform
