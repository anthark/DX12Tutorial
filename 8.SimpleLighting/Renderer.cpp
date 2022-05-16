#include "stdafx.h"
#include "Renderer.h"

#include "Platform.h"
#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformShapes.h"
#include "PlatformTexture.h"
#include "PlatformIO.h"
#include "PlatformUtil.h"

#include "Light.h"

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
    Point3f normal;
};

struct TextVertex
{
    Point3f pos;
    Point3f color;
    Point2f uv;
};

}

Renderer::Renderer(Platform::Device* pDevice)
    : Platform::BaseRenderer(pDevice, 2, 0, { sizeof(Matrix4f), sizeof(Lights) })
    , CameraControlEuler()
    , m_pTextDraw(nullptr)
    , m_fpsCount(0)
    , m_prevFPS(0.0)
    , m_fps(0.0)
{
}

Renderer::~Renderer()
{
    assert(m_pTextDraw == nullptr);
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
            res = Platform::CreateTextureFromFile(_T("../Common/Plaster.png"), GetDevice(), m_plasterTextureResource);
        }

        if (res)
        {
            m_pTextDraw = new Platform::TextDraw();
            res = m_pTextDraw->Init(this);
            if (res)
            {
                m_pTextDraw->Resize(GetRect());
            }
        }
        if (res)
        {
            res = m_pTextDraw->CreateFont(_T("../Common/terminus.ttf"), 24, m_fontId);
        }

        if (res)
        {
            Geometry geometry;
            CreateGeometryParams params;

            Platform::GetCubeDataSize(false, true, indexCount, vertexCount);
            cubeVertices.resize(vertexCount);
            indices.resize(indexCount);
            Platform::CreateCube(indices.data(), sizeof(TextureVertex), &cubeVertices[0].pos, nullptr, &cubeVertices[0].uv, &cubeVertices[0].normal);

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12 });
            params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 20 });
            params.indexDataSize = (UINT)indices.size() * sizeof(UINT16);
            params.indexFormat = DXGI_FORMAT_R16_UINT;
            params.pIndices = indices.data();
            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("LightTexture.hlsl");
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

            // Now create plane
            if (res)
            {
                params.geomStaticTextures.clear();
                params.geomStaticTextures.push_back(m_plasterTextureResource.pResource);

                static const UINT16 planeIndices[6] = {0,1,2,0,2,3};
                static const TextureVertex planeVertices[4] = {
                    {{-4, 0.1f, -2}, {0, 0}, {0, 1, 0}},
                    {{-4, 0.1f,  2}, {0, 1}, {0, 1, 0}},
                    {{ -1, 0.1f,  2}, {1, 1}, {0, 1, 0}},
                    {{ -1, 0.1f, -2}, {1, 0}, {0, 1, 0}}
                };

                params.indexDataSize = 6 * sizeof(UINT16);
                params.pIndices = planeIndices;
                params.pVertices = planeVertices;
                params.vertexDataSize = 4 * sizeof(TextureVertex);
                res = CreateGeometry(params, geometry);
                if (res)
                {
                    m_geometries.push_back(geometry);
                }
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
    }

    return res;
}

void Renderer::Term()
{
    GetDevice()->WaitGPUIdle();

    TERM_RELEASE(m_pTextDraw);

    for (auto geometry : m_geometries)
    {
        DestroyGeometry(geometry);
    }
    m_geometries.clear();

    GetDevice()->ReleaseGPUResource(m_textureResource);
    GetDevice()->ReleaseGPUResource(m_plasterTextureResource);

    Platform::BaseRenderer::Term();
}

bool Renderer::Update(double elapsedSec, double deltaSec)
{
    m_angle += M_PI / 2 * deltaSec;

    UpdateCamera(deltaSec);

    Matrix4f rotation;
    rotation.Rotation((float)m_angle, Point3f{ 0, 1, 0 });

    m_geometries[0].objCB.trans = rotation;
    m_geometries[0].objCB.transNormals = rotation.Inverse().Transpose();

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
    m_pTextDraw->ResetCaret();

    ++m_fpsCount;

    D3D12_RECT rect = GetRect();
    float aspectRatioHdivW = (float)(rect.bottom - rect.top) / (rect.right - rect.left);

    UINT8* dynCBData[2] = {};
    BeginRenderParams beginParams = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        dynCBData
    };

    if (BeginRender(beginParams))
    {
        // Setup VP matrix
        Matrix4f vp = GetCamera()->CalcViewMatrix() * GetCamera()->CalcProjMatrix(aspectRatioHdivW);
        memcpy(dynCBData[0], &vp, sizeof(vp));

        // Setup lights data
        SetupLights(reinterpret_cast<Lights*>(dynCBData[1]));

        for (const auto& geometry : m_geometries)
        {
            RenderGeometry(geometry);
        }

        m_pTextDraw->DrawText(m_fontId, Point3f{ 1,1,1 }, _T("FPS: %5.2f"), m_fps);

        EndRender();

        return true;
    }

    return false;
}

void Renderer::SetupLights(Lights* pLights)
{
    pLights->ambientColor = Point3f{ 0.1f, 0.1f, 0.1f };

    unsigned int idx = 0;

    // Add point light
    {
        pLights->lights[idx].type = LT_Point;
        pLights->lights[idx].pos = Point3f{ -1.5f, 0.5f, 0 };
        pLights->lights[idx].color = Point4f{ 0, 0.6f, 0, 0 };

        ++idx;
    }

    // Add directional light
    {
        pLights->lights[idx].type = LT_Direction;
        pLights->lights[idx].dir = Point4f{ -1, -1, -1, 0 };
        pLights->lights[idx].dir.normalize();
        pLights->lights[idx].color = Point4f{ 0.2f, 0, 0, 0 };

        ++idx;
    }

    // Add spot light
    {
        pLights->lights[idx].type = LT_Spot;
        pLights->lights[idx].pos = Point3f{ -2.5f, 0.2f, 1.5f };
        pLights->lights[idx].dir = Point4f{ 1, -0.5f, -1, 0 };
        pLights->lights[idx].dir.normalize();
        pLights->lights[idx].color = Point4f{ 0, 0, 0.8f, 0 };
        pLights->lights[idx].falloffAngles = Point4f{ (float)M_PI / 6, (float)M_PI / 12, 0, 0 };

        ++idx;
    }

    pLights->lightCount = idx;
}
