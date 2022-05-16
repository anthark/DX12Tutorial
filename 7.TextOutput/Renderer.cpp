#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformTexture.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"

#include <chrono>

#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <algorithm>

namespace
{

struct ColorVertex
{
    Point3f pos;
    Point3f color;
};

struct TextureVertex
{
    Point3f pos;
    Point2f uv;
};

struct TextVertex
{
    Point3f pos;
    Point3f color;
    Point2f uv;
};

}

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 1, 0, { sizeof(Matrix4f) })
    , CameraControlEuler()
    , m_ftLibrary(nullptr)
    , m_fpsCount(0)
    , m_prevFPS(0.0)
    , m_fps(0.0)
{
}

Renderer::~Renderer()
{
    assert(m_ftLibrary == nullptr);
}

bool Renderer::Init(HWND hWnd)
{
    static const int GridCells = 10;
    static const float GridStep = 1.0f;

    size_t vertexCount = 0;
    size_t indexCount = 0;
    std::vector<ColorVertex> gridVertices;
    std::vector<TextureVertex> cubeVertices;
    std::vector<UINT16> indices;

    bool res = Platform::BaseRenderer::Init(hWnd);
    if (res)
    {
        res = BeginGeometryCreation();
        if (res)
        {
            res = Platform::CreateTextureFromFile(_T("../Common/Kitty.png"), GetDevice(), m_textureResource);
        }

        if (res)
        {
            FT_Error error = FT_Init_FreeType(&m_ftLibrary);
            assert(error == 0);
            res = error == 0;
        }
        if (res)
        {
            res = CreateFont(_T("../Common/terminus.ttf"), 24);
        }
        if (res)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuTextureHandle;
            res = GetDevice()->AllocateStaticDescriptors(1, cpuTextureHandle, m_fontTextureHandle);
            if (res)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
                texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                texDesc.Format = m_fontResource.pResource->GetDesc().Format;
                texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                texDesc.Texture2D.MipLevels = 1;
                GetDevice()->GetDXDevice()->CreateShaderResourceView(m_fontResource.pResource, &texDesc, cpuTextureHandle);

                cpuTextureHandle.ptr += GetDevice()->GetDXDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }

        if (res)
        {
            Geometry geometry;
            CreateGeometryParams params;

            Platform::GetCubeDataSize(false, true, indexCount, vertexCount);
            cubeVertices.resize(vertexCount);
            indices.resize(indexCount);
            Platform::CreateCube(indices.data(), sizeof(TextureVertex), &cubeVertices[0].pos, nullptr, &cubeVertices[0].uv);

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
            params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
            params.indexFormat = DXGI_FORMAT_R16_UINT;
            params.pIndices = indices.data();
            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("SimpleTexture.hlsl");
            params.pVertices = cubeVertices.data();
            params.vertexDataSize = (UINT)cubeVertices.size() * sizeof(TextureVertex);
            params.vertexDataStride = sizeof(TextureVertex);

            params.geomStaticTextures.push_back(m_textureResource.pResource);
            params.geomStaticTexturesCount = 1;

            res = CreateGeometry(params, geometry);
            if (res)
            {
                m_geometries.push_back(geometry);
            }
            if (res)
            {
                geometry = Geometry();
                indices.clear();

                Platform::GetGridDataSize(GridCells, indexCount, vertexCount);
                gridVertices.resize(vertexCount);
                indices.resize(indexCount);
                Platform::CreateGrid(GridCells, GridStep, indices.data(), sizeof(ColorVertex), &gridVertices[0].pos);
                for (auto& vertex : gridVertices)
                {
                    vertex.color = Point3f{ 1,1,1 };
                }

                params.geomAttributes.clear();
                params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
                params.geomAttributes.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
                params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
                params.indexFormat = DXGI_FORMAT_R16_UINT;
                params.pIndices = indices.data();
                params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                params.pShaderSourceName = _T("SimpleColor.hlsl");
                params.pVertices = gridVertices.data();
                params.vertexDataSize = (UINT)gridVertices.size() * sizeof(ColorVertex);
                params.vertexDataStride = sizeof(ColorVertex);

                params.geomStaticTextures.clear();
                params.geomStaticTexturesCount = 0;

                res = CreateGeometry(params, geometry);
            }
            if (res)
            {
                m_geometries.push_back(geometry);
            }

            EndGeometryCreation();
        }

        if (res)
        {
            GeometryStateParams geomStateParams;
            geomStateParams.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            geomStateParams.geomAttributes.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
            geomStateParams.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 24 });
            geomStateParams.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            geomStateParams.pShaderSourceName = _T("Text.hlsl");
            geomStateParams.geomStaticTexturesCount = 1;
            geomStateParams.depthStencilState.DepthEnable = FALSE;
            geomStateParams.blendState.RenderTarget[0].BlendEnable = TRUE;

            geomStateParams.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            geomStateParams.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            geomStateParams.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            geomStateParams.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            geomStateParams.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            geomStateParams.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

            res = CreateGeometryState(geomStateParams, m_textGeomState);
        }
    }

    return res;
}

void Renderer::Term()
{
    if (m_ftLibrary != nullptr)
    {
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
    }

    for (auto geometry : m_geometries)
    {
        DestroyGeometry(geometry);
    }
    m_geometries.clear();

    DestroyGeometryState(m_textGeomState);

    GetDevice()->ReleaseGPUResource(m_fontResource);
    GetDevice()->ReleaseGPUResource(m_textureResource);

    Platform::BaseRenderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 2 * deltaSec;

    UpdateCamera(deltaSec);

    Matrix4f rotation;
    rotation.Rotation((float)m_angle, Point3f{ 0, 1, 0 });

    m_geometries[0].trans = rotation;

    if (elapsedSec - m_prevFPS >= 1.0)
    {
        m_fps = m_fpsCount;
        m_fpsCount = 0;

        m_prevFPS = elapsedSec;
    }

    return true;
}

bool Renderer::Render()
{
    ++m_fpsCount;

    D3D12_RECT rect = GetRect();
    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    UINT8* dynCBData[1] = {};
    BeginRenderParams beginParams = {
        {0.5f, 0.5f, 0.5f, 1.0f},
        dynCBData
    };
    if (BeginRender(beginParams))
    {
        Matrix4f vp = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);
        memcpy(dynCBData[0], &vp, sizeof(vp));

        for (const auto& geometry : m_geometries)
        {
            RenderGeometry(geometry);
        }

        TCHAR buffer[16];
        _stprintf_s(buffer, _T("FPS: %5.2f"), m_fps);
        DrawText(buffer, Point3f{ 1,1,1 });

        EndRender();

        return true;
    }

    return false;
}

bool Renderer::CreateFont(LPCTSTR fontFilename, int fontSize)
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

    // Render glyphs and 
    UINT8* pHostBuffer = nullptr;
    if (error == 0)
    {
        m_symbolsData.resize(symbolCount);

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

                m_symbolsData[idx].leftTop = Point2f{ (float)symbolX / textureWidth, (float)symbolY / textureHeight };
                m_symbolsData[idx].rightBottom = Point2f{ (float)(symbolX + slot->bitmap.width) / textureWidth, (float)(symbolY + slot->bitmap.rows) / textureHeight };
                m_symbolsData[idx].symbolSize = Point2f{ (float)slot->bitmap.width, (float)slot->bitmap.rows };
                m_symbolsData[idx].advance = slot->advance.x >> 6;
                m_symbolsData[idx].base = Point2f{ (float)slot->bitmap_left, (float)slot->bitmap_top };
            }
        } // Cycle over symbols
    }

    // Create texture
    if (error == 0 && pHostBuffer != nullptr)
    {
        Platform::CreateTextureParams params = {textureWidth, textureHeight, DXGI_FORMAT_A8_UNORM};

        error = Platform::CreateTexture(params, false, GetDevice(), m_fontResource, pHostBuffer, textureHeight * textureWidth) ? 0 : 1;

        delete[] pHostBuffer;
        pHostBuffer = nullptr;
    }

    assert(error == 0);

    return error == 0;
}

void Renderer::DrawText(LPCTSTR text, const Point3f& color)
{
    UINT count = (UINT)_tcslen(text);

    UINT64 gpuVirtualAddress;

    TextVertex* pVertices = nullptr;
    UINT vertexBufferSize = (UINT)(count * 4 * sizeof(TextVertex));
    bool res = GetDevice()->AllocateDynamicBuffer(vertexBufferSize, 1, (void**)&pVertices, gpuVirtualAddress);
    if (res)
    {
        // Fill

        // Calculate Y bearing
        float bearing = 0;
        for (UINT i = 0; i < count; i++)
        {
            const SymbolData& data = m_symbolsData[text[i] - 0x20];
            bearing = std::max(bearing, data.base.y);
        }


        // Fill letter's data
        D3D12_RECT rect = GetRect();
        float divX = 2.0f / (rect.right - rect.left);
        float divY = 2.0f / (rect.bottom - rect.top);

        Point3f localPos{0, bearing, 0};
        for (UINT i = 0; i < count; i++)
        {
            const SymbolData& data = m_symbolsData[text[i] - 0x20];

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

        GetCurrentCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
    }

    if (res)
    {
        UINT16* pIndices = nullptr;
        UINT indexBufferSize = (UINT)(count * 6 * sizeof(UINT16));
        res = GetDevice()->AllocateDynamicBuffer(indexBufferSize, 1, (void**)&pIndices, gpuVirtualAddress);
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

            GetCurrentCommandList()->IASetIndexBuffer(&indexBufferView);
        }
    }

    if (res)
    {
        SetupGeometryState(m_textGeomState);

        GetCurrentCommandList()->SetGraphicsRootDescriptorTable(3, m_fontTextureHandle);

        GetCurrentCommandList()->DrawIndexedInstanced(count * 6, 1, 0, 0, 0);
    }
}
