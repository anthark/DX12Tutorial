#pragma once

#include "PlatformDevice.h"
#include "PlatformMatrix.h"
#include "PlatformBaseRenderer.h"
#include "PlatformUtil.h"

#include "..\..\Common\Shaders\GLTFObjectData.h"

namespace tinygltf
{
class Model;
struct Image;
class Node;
}

namespace Platform
{

class BaseRenderer;

struct GLTFSplitData
GLTF_SPLIT_DATA

struct GLTFObjectData
GLTF_OBJECT_DATA

struct GLTFGeometry : public BaseRenderer::Geometry
{
    virtual const void* GetObjCB(size_t& size) const override { size = sizeof(splitData); return &splitData; }

    GLTFSplitData splitData;
};

enum ZPassType
{
    ZPassTypeSimple = 0,
    ZPassTypeBias,
    ZPassTypeBiasSlopeScale,

    ZPassGBuffer,

    ZPassTypeCount
};

struct ZPassState
{
    BaseRenderer::GeometryState* states[ZPassTypeCount] = { 0 };
};

struct PLATFORM_API GLTFModel
{
    struct Node
    {
        bool useMatrix;
        union
        {
            Matrix4f matrix;
            struct {
                Point4f rotation;
                Point3f translation;
                Point3f scale;
            } transform;
        };

        Node(const Matrix4f& matrix = Matrix4f())
            : useMatrix(true)
            , matrix(matrix)
        {}

        Node(const Point4f& rotation, const Point3f& translation, const Point3f& scale)
            : useMatrix(false)
        {
            transform.rotation = rotation;
            transform.translation = translation;
            transform.scale = scale;
        }

        std::vector<int> children;
    };

    struct AnimationSampler
    {
        // Assumed it is always linear interpolation here
        std::vector<float> timeKeys;   // Time key values
        std::vector<Point4f> keys;      // Key values
    };

    struct AnimationChannel
    {
        enum Type
        {
            Rotation,
            Translation,
            Scale
        };

        int animSamplerIdx;
        int nodeIdx;
        Type type;
    };

    bool skinned = false;

    int rootNodeIdx;
    std::vector<Node> nodes;
    std::vector<Matrix4f> nodeInvBindMatrices;
    GLTFObjectData objData;

    std::vector<GLTFGeometry*> geometries;
    std::vector<GLTFGeometry*> blendGeometries;

    std::vector<ZPassState> zPassGeomStates;
    std::vector<BaseRenderer::GeometryState*> cubePassStates;

    std::vector<Platform::GPUResource> modelTextures;

    Point4f bbMin;
    Point4f bbMax;
    float scaleValue = 1.0f;
    Point3f offset = Point3f();

    std::wstring name;

    float maxAnimationTime = 0.0f;
    float minAnimationTime = 0.0f;
    std::vector<AnimationSampler> animationSamplers;
    std::vector<AnimationChannel> animationChannels;

    void Term(BaseRenderer* pRenderer);

    void UpdateMatrices();
    void UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent);
};

struct PLATFORM_API GLTFModelInstance
{
    const GLTFModel* pModel;
    Point3f pos;
    float angle;

    std::vector<GLTFModel::Node> nodes;

    float animationTime = 0.0f;

    std::vector<GLTFSplitData> instGeomData;
    std::vector<GLTFSplitData> instBlendGeomData;

    GLTFObjectData instObjData;

    void SetPos(const Point3f& _pos);
    void SetAngle(float _angle);

    Matrix4f CalcTransform() const;

    void ApplyAnimation();
    void UpdateMatrices();
    void UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent, const Matrix4f& parentNormals);

private:
    void SetupTransform();
};

class PLATFORM_API ModelLoader
{
public:
    ModelLoader();
    virtual ~ModelLoader();

    bool Init(BaseRenderer* pRenderer, const std::vector<std::tstring>& modelFiles, const DXGI_FORMAT hdrFormat, const DXGI_FORMAT cubeHDRFormat, bool forDeferred, bool useLocalCubemaps);
    void Term();

    inline bool HasModelsToLoad() const { return !m_modelFiles.empty(); }
    inline std::tstring GetCurrentModelName() const { return m_modelFiles.front(); }

    bool ProcessModelLoad();

    inline size_t GetModelCount() const { return m_models.size(); }
    inline const GLTFModel* GetModel(size_t idx) const { return m_models[idx]; }
    inline GLTFModel* GetModel(size_t idx) { return m_models[idx]; }

    inline const std::vector<std::string>& GetLoadedModels() const { return m_loadedModels; }

    Platform::GLTFModel* FindModel(const std::wstring& modelName) const;

private:

    struct ModelLoadState
    {
        GLTFModel* pGLTFModel = nullptr;
        tinygltf::Model* pModel = nullptr;
        std::vector<Platform::GPUResource> modelTextures;
        std::vector<bool> modelSRGB;
        bool autoscale = true;
        float scaleValue = 1.0f;

        void ClearState();
    };

private:
    bool LoadModel(const std::tstring& name, tinygltf::Model** ppModel);
    bool ScanTexture(const tinygltf::Image& image, bool srgb, Platform::GPUResource& texture);
    bool ScanNode(const tinygltf::Model& model, int nodeIdx, const std::vector<Platform::GPUResource>& textures);
    AABB<float> CalcModelAABB(const tinygltf::Model& model, int nodeIdx, const Matrix4f* pTransforms);
    void SetupModelScale();
    void LoadSkins();
    void LoadAnimations();

private:
    std::vector<GLTFModel*> m_models;

    std::vector<std::tstring> m_modelFiles;
    BaseRenderer* m_pRenderer;
    DXGI_FORMAT m_hdrFormat;
    DXGI_FORMAT m_cubeHDRFormat;
    bool m_useLocalCubemaps;
    bool m_forDeferred;

    ModelLoadState m_modelLoadState;

    std::vector<std::string> m_loadedModels;
};

} // Platform
