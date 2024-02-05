#include "stdafx.h"
#include "PlatformModelLoader.h"

#include "PlatformTexture.h"
#include "PlatformUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

namespace Platform
{

void GLTFModel::Term(BaseRenderer* pRenderer)
{
    for (auto state : cubePassStates)
    {
        pRenderer->DestroyGeometryState(*state);
    }
    cubePassStates.clear();

    for (auto state : zPassGeomStates)
    {
        for (int i = 0; i < ZPassTypeCount; i++)
        {
            if (state.states[i] != nullptr)
            {
                pRenderer->DestroyGeometryState(*(state.states[i]));
            }
        }
    }
    zPassGeomStates.clear();

    for (auto geometry : geometries)
    {
        pRenderer->DestroyGeometry(*geometry);
    }
    geometries.clear();

    for (auto geometry : blendGeometries)
    {
        pRenderer->DestroyGeometry(*geometry);
    }
    blendGeometries.clear();

    for (auto texture : modelTextures)
    {
        pRenderer->GetDevice()->ReleaseGPUResource(texture);
    }
    modelTextures.clear();
}

void GLTFModel::UpdateMatrices()
{
    Matrix4f m;
    m.Identity();
    m.m[0] = -m.m[0];

    Matrix4f scale;
    scale.Scale(scaleValue, scaleValue, scaleValue);

    Matrix4f trans;
    trans.Offset(offset);

    m = m * trans * scale;

    UpdateNodeMatrices(rootNodeIdx, m);
}

void GLTFModel::UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent)
{
    const Node& node = nodes[nodeIdx];

    Matrix4f m;

    if (node.useMatrix)
    {
        m = node.matrix;
    }
    else
    {
        Matrix4f rotationMatrix;
        Matrix4f translationMatrix;
        Matrix4f scaleMatrix;

        rotationMatrix.FromQuaternion(node.transform.rotation);
        translationMatrix.Offset(node.transform.translation);
        scaleMatrix.Scale(node.transform.scale.x, node.transform.scale.y, node.transform.scale.z);

        m = scaleMatrix * rotationMatrix * translationMatrix;
    }

    m = m * parent;

    Matrix4f fullMatrix = nodeInvBindMatrices[nodeIdx] * m;

    objData.nodeTransforms[nodeIdx] = fullMatrix;
    objData.nodeNormalTransforms[nodeIdx] = fullMatrix.Inverse().Transpose();

    for (auto idx : node.children)
    {
        UpdateNodeMatrices(idx, m);
    }
}

void ModelLoader::ModelLoadState::ClearState()
{
    pGLTFModel = nullptr;

    delete pModel;
    pModel = nullptr;

    modelTextures.clear();
    modelSRGB.clear();

    autoscale = true;
    scaleValue = 1.0f;
}

void GLTFModelInstance::SetPos(const Point3f& _pos)
{
    if ((pos - _pos).length() > 0.0001f)
    {
        pos = _pos;
        SetupTransform();
    }
}

void GLTFModelInstance::SetAngle(float _angle)
{
    if (fabs(angle - _angle) > 0.0001f)
    {
        angle = _angle;
        SetupTransform();
    }
}

Matrix4f GLTFModelInstance::CalcTransform() const
{
    Matrix4f trans;

    trans.Offset(pos);
    Matrix4f rotate;
    rotate.Rotation(angle, Point3f{ 0,1,0 });
    trans = rotate * trans;

    return trans;
}

void GLTFModelInstance::ApplyAnimation()
{
    if (pModel->maxAnimationTime == 0.0f) // No animation
    {
        return;
    }

    float localTime = animationTime;
    while (localTime > pModel->maxAnimationTime)
    {
        localTime -= int(localTime / pModel->maxAnimationTime) * pModel->maxAnimationTime;
    }

    for (int i = 0; i < pModel->animationChannels.size(); i++)
    {
        const GLTFModel::AnimationSampler& sampler = pModel->animationSamplers[pModel->animationChannels[i].animSamplerIdx];

        Point4f value;
        if (sampler.timeKeys.size() == 1)
        {
            value = sampler.timeKeys[0];
        }
        else
        {
            int idx0, idx1;
            float ratio;

            if (localTime < sampler.timeKeys.front())
            {
                idx0 = (int)sampler.keys.size() - 1;
                idx1 = 0;

                ratio = localTime / sampler.timeKeys.front();
            }
            else
            {
                for (int j = 1; j < sampler.timeKeys.size(); j++)
                {
                    if (localTime < sampler.timeKeys[j])
                    {
                        idx0 = j - 1;
                        idx1 = j;

                        ratio = (localTime - sampler.timeKeys[j - 1]) / (sampler.timeKeys[j] - sampler.timeKeys[j - 1]);

                        break;
                    }
                }
            }

            if (pModel->animationChannels[i].type == GLTFModel::AnimationChannel::Rotation)
            {
                value = Point4f::Slerp(sampler.keys[idx0], sampler.keys[idx1], ratio);
            }
            else
            {
                value = sampler.keys[idx0] * (1.0f - ratio) + sampler.keys[idx1] * ratio;
            }
        }

        if (nodes[pModel->animationChannels[i].nodeIdx].useMatrix == true)
        {
            nodes[pModel->animationChannels[i].nodeIdx].transform.rotation = Point4f(0, 0, 0, 1);
            nodes[pModel->animationChannels[i].nodeIdx].transform.translation = Point3f(0, 0, 0);
            nodes[pModel->animationChannels[i].nodeIdx].transform.scale = Point3f(1, 1, 1);

            nodes[pModel->animationChannels[i].nodeIdx].useMatrix = false;
        }
        switch (pModel->animationChannels[i].type)
        {
            case GLTFModel::AnimationChannel::Rotation:
                nodes[pModel->animationChannels[i].nodeIdx].transform.rotation = value;
                break;

            case GLTFModel::AnimationChannel::Translation:
                nodes[pModel->animationChannels[i].nodeIdx].transform.translation = value;
                break;

            case GLTFModel::AnimationChannel::Scale:
                nodes[pModel->animationChannels[i].nodeIdx].transform.scale = value;
                break;
        }
    }
}

void GLTFModelInstance::UpdateMatrices()
{
    Matrix4f m;
    m.Identity();
    m.m[0] = -m.m[0];

    Matrix4f normM = m; // Let's don't use overall model scaling and offset for normals

    Matrix4f scale;
    scale.Scale(pModel->scaleValue, pModel->scaleValue, pModel->scaleValue);

    Matrix4f trans;
    trans.Offset(pModel->offset);

    m = m * trans * scale;

    if (!pModel->nodes.empty())
    {
        UpdateNodeMatrices(pModel->rootNodeIdx, m, normM);
    }
}

void GLTFModelInstance::UpdateNodeMatrices(int nodeIdx, const Matrix4f& parent, const Matrix4f& parentNormals)
{
    const GLTFModel::Node& node = nodes[nodeIdx];

    Matrix4f m;

    if (node.useMatrix)
    {
        m = node.matrix;
    }
    else
    {
        Matrix4f rotationMatrix;
        Matrix4f translationMatrix;
        Matrix4f scaleMatrix;

        rotationMatrix.FromQuaternion(node.transform.rotation);
        translationMatrix.Offset(node.transform.translation);
        scaleMatrix.Scale(node.transform.scale.x, node.transform.scale.y, node.transform.scale.z);

        m = scaleMatrix * rotationMatrix * translationMatrix;
    }

    Matrix4f normM = m * parentNormals;

    m = m * parent;

    Matrix4f fullMatrix = pModel->nodeInvBindMatrices[nodeIdx] * m;

    instObjData.nodeTransforms[nodeIdx] = fullMatrix;
    instObjData.nodeNormalTransforms[nodeIdx] = (pModel->nodeInvBindMatrices[nodeIdx] * normM).Inverse().Transpose();

    for (auto idx : node.children)
    {
        UpdateNodeMatrices(idx, m, normM);
    }
}

void GLTFModelInstance::SetupTransform()
{
    Matrix4f trans = CalcTransform();
    Matrix4f normalTrans = trans.Inverse().Transpose();

    instObjData.modelTransform = trans;
    instObjData.modelNormalTransform = normalTrans;

    for (int j = 0; j < pModel->geometries.size(); j++)
    {
        instGeomData[j].transform = pModel->geometries[j]->splitData.transform * trans;
        instGeomData[j].transformNormals = pModel->geometries[j]->splitData.transformNormals * normalTrans;
    }
    for (int j = 0; j < pModel->blendGeometries.size(); j++)
    {
        instBlendGeomData[j].transform = pModel->blendGeometries[j]->splitData.transform * trans;
        instBlendGeomData[j].transformNormals = pModel->blendGeometries[j]->splitData.transformNormals * normalTrans;
    }
}

ModelLoader::ModelLoader(bool zPassNormals)
    : m_pRenderer(nullptr)
    , m_modelLoadState()
    , m_zPassNormals(zPassNormals)
{
}

ModelLoader::~ModelLoader()
{
    assert(m_pRenderer == nullptr);
    assert(m_modelFiles.empty());
}

bool ModelLoader::Init(BaseRenderer* pRenderer, const std::vector<std::tstring>& modelFiles, const DXGI_FORMAT hdrFormat, const DXGI_FORMAT cubeHDRFormat, bool forDeferred, bool useLocalCubemaps)
{
    m_pRenderer = pRenderer;
    m_useLocalCubemaps = useLocalCubemaps;
    m_forDeferred = forDeferred;

    bool res = true;

    if (res)
    {
        m_modelFiles = modelFiles;

        m_hdrFormat = hdrFormat;
        m_cubeHDRFormat = cubeHDRFormat;
    }

    return res;
}

void ModelLoader::Term()
{
    for (auto model : m_models)
    {
        model->Term(m_pRenderer);
    }
    m_models.clear();

    m_pRenderer = nullptr;
}

bool ModelLoader::ProcessModelLoad()
{
    bool res = true;
    if (m_modelLoadState.pModel == nullptr)
    {
        std::wstring name = m_modelFiles.front();

        res = LoadModel(name, &m_modelLoadState.pModel);

        if (res)
        {
            m_modelLoadState.modelSRGB.resize(m_modelLoadState.pModel->images.size(), false);

            for (int i = 0; i < m_modelLoadState.pModel->materials.size(); i++)
            {
                int diffuseIdx = -1;
                int specGlossIdx = -1;
                const tinygltf::Material& material = m_modelLoadState.pModel->materials[i];
                if (material.extensions.find("KHR_materials_pbrSpecularGlossiness") != material.extensions.end())
                {
                    const tinygltf::Value& diff = material.extensions.at("KHR_materials_pbrSpecularGlossiness").Get("diffuseTexture");
                    if (diff.IsObject())
                    {
                        diffuseIdx = diff.Get("index").GetNumberAsInt();
                    }

                    const tinygltf::Value& specGlos = material.extensions.at("KHR_materials_pbrSpecularGlossiness").Get("specularGlossinessTexture");
                    if (specGlos.IsObject())
                    {
                        specGlossIdx = specGlos.Get("index").GetNumberAsInt();
                    }
                }
                else
                {
                    diffuseIdx = material.pbrMetallicRoughness.baseColorTexture.index;
                }

                if (diffuseIdx != -1)
                {
                    int imageIdx = m_modelLoadState.pModel->textures[diffuseIdx].source;
                    m_modelLoadState.modelSRGB[imageIdx] = true;
                }
                if (specGlossIdx != -1)
                {
                    int imageIdx = m_modelLoadState.pModel->textures[specGlossIdx].source;
                    m_modelLoadState.modelSRGB[imageIdx] = true;
                }
            }
        }
        if (res)
        {
            m_modelLoadState.pGLTFModel = new GLTFModel();

            LoadSkins();
            LoadAnimations();
        }
    }
    else if (m_modelLoadState.modelTextures.size() < m_modelLoadState.pModel->images.size())
    {
        // Process texture loading
        Platform::GPUResource texture;
        res = m_pRenderer->BeginGeometryCreation();
        if (res)
        {
            res = ScanTexture(m_modelLoadState.pModel->images[m_modelLoadState.modelTextures.size()], m_modelLoadState.modelSRGB[m_modelLoadState.modelTextures.size()], texture);
            if (res)
            {
                m_modelLoadState.modelTextures.push_back(texture);
            }
            m_pRenderer->EndGeometryCreation();
        }
    }
    else
    {
        res = m_pRenderer->BeginGeometryCreation();
        if (res)
        {
            res = ScanNode(*m_modelLoadState.pModel, m_modelLoadState.pModel->scenes[0].nodes[0], m_modelLoadState.modelTextures);
            if (res)
            {
                m_modelLoadState.pGLTFModel->rootNodeIdx = m_modelLoadState.pModel->scenes[0].nodes[0];
                m_modelLoadState.pGLTFModel->nodeInvBindMatrices.resize(m_modelLoadState.pGLTFModel->nodes.size());

                assert(m_modelLoadState.pGLTFModel->nodes.size() <= MAX_NODES);
                m_modelLoadState.pGLTFModel->UpdateMatrices();

                SetupModelScale();

                m_modelLoadState.pGLTFModel->modelTextures = m_modelLoadState.modelTextures;
            }

            m_pRenderer->EndGeometryCreation();
        }
        
        if (res)
        {
            m_models.push_back(m_modelLoadState.pGLTFModel);
        }

#ifdef UNICODE
        m_modelLoadState.pGLTFModel->name = m_modelFiles.front();

        char buffer[MAX_PATH + 1];
        sprintf(buffer, "%ls", GetParentName(m_modelFiles.front()).c_str());

        m_loadedModels.push_back(buffer);
#else
        wchar_t buffer[MAX_PATH + 1];
        wsprintf(buffer, L"%s", m_modelFiles.front().c_str());
        m_modelLoadState.pGLTFModel->name = buffer;

        m_loadedModels.push_back(GetParentName(m_modelFiles.front()));
#endif // !UNICODE

        m_modelLoadState.ClearState();

        m_modelFiles.erase(m_modelFiles.begin());
    }

    return res;
}

Platform::GLTFModel* ModelLoader::FindModel(const std::wstring& modelName) const
{
    for (size_t i = 0; i < m_models.size(); i++)
    {
        if (modelName == m_models[i]->name)
        {
            return m_models[i];
        }
    }

    return nullptr;
}

bool ModelLoader::LoadModel(const std::tstring& name, tinygltf::Model** ppModel)
{
    *ppModel = new tinygltf::Model();
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

#ifdef UNICODE
    char buffer[MAX_PATH + 1];
    sprintf(buffer, "%ls", name.c_str());

    bool ret = loader.LoadASCIIFromFile(*ppModel, &err, &warn, buffer);
#else
    bool ret = loader.LoadASCIIFromFile(*ppModel, &err, &warn, name.c_str());
#endif // !UNICODE

    if (ret && (*ppModel)->asset.extras.IsObject())
    {
        const tinygltf::Value& scaleFlag = (*ppModel)->asset.extras.Get("autoscale");
        if (scaleFlag.IsBool())
        {
            m_modelLoadState.autoscale = scaleFlag.Get<bool>();
        }

        const tinygltf::Value& scaleValue = (*ppModel)->asset.extras.Get("scale");
        if (scaleValue.IsNumber())
        {
            m_modelLoadState.scaleValue = (float)scaleValue.Get<double>();
        }
    }

    return ret;
}

bool ModelLoader::ScanTexture(const tinygltf::Image& image, bool srgb, Platform::GPUResource& texture)
{
    Platform::CreateTextureParams params;
    params.width = image.width;
    params.height = image.height;
    assert(image.component == 4);
    assert(image.bits == 8);
    assert(image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE);
    params.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    return Platform::CreateTexture(params, true, m_pRenderer->GetDevice(), texture, image.image.data(), image.image.size());
}

bool ModelLoader::ScanNode(const tinygltf::Model& model, int nodeIdx, const std::vector<Platform::GPUResource>& textures)
{
    const tinygltf::Node& node = model.nodes[nodeIdx];

    struct NormalVertex
    {
        Point3f pos;
        Point3f normal;
        Point4f tangent;
        Point2f uv;
    };

    struct NormalWeightedVertex
    {
        Point3f pos;
        Point3f normal;
        Point4f tangent;
        Point2f uv;
        Point4<unsigned short> joints;
        Point4f weights;
    };

    bool res = true;

    if (nodeIdx >= m_modelLoadState.pGLTFModel->nodes.size())
    {
        m_modelLoadState.pGLTFModel->nodes.resize(nodeIdx + 1);
    }

    if (node.matrix.size() != 0)
    {
        assert(node.matrix.size() == 16);
        Matrix4f nodeMatrix;
        for (int i = 0; i < 16; i++)
        {
            nodeMatrix.m[i] = (float)node.matrix[i];
        }
        m_modelLoadState.pGLTFModel->nodes[nodeIdx] = GLTFModel::Node(nodeMatrix);
    }
    else
    {
        Point4f r = Point4f(0, 0, 0, 1);    // Identity rotation
        Point3f t = Point3f(0, 0, 0);       // No translation
        Point3f s = Point3f(1, 1, 1);       // No scaling

        if (node.rotation.size() != 0)
        {
            assert(node.rotation.size() == 4);
            r = Point4f((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
        }
        if (node.translation.size() != 0)
        {
            assert(node.translation.size() == 3);
            t = Point3f((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
        }
        if (node.scale.size() != 0)
        {
            assert(node.scale.size() == 3);
            s = Point3f((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
        }

        m_modelLoadState.pGLTFModel->nodes[nodeIdx] = GLTFModel::Node(r, t, s);
    }

    if (node.mesh != -1)
    {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];

        for (int i = 0; i < mesh.primitives.size(); i++)
        {
            const tinygltf::Primitive& prim = mesh.primitives[i];
            assert(prim.mode == TINYGLTF_MODE_TRIANGLES);

            int posIdx = prim.attributes.find("POSITION")->second;
            int normIdx = prim.attributes.find("NORMAL")->second;
            int uvIdx = prim.attributes.find("TEXCOORD_0")->second;
            int tgIdx = -1;
            if (prim.attributes.find("TANGENT") != prim.attributes.end())
            {
                tgIdx = prim.attributes.find("TANGENT")->second;
            }
            int jointsIdx = -1;
            if (prim.attributes.find("JOINTS_0") != prim.attributes.end())
            {
                jointsIdx = prim.attributes.find("JOINTS_0")->second;
            }
            int weightsIdx = -1;
            if (prim.attributes.find("WEIGHTS_0") != prim.attributes.end())
            {
                weightsIdx = prim.attributes.find("WEIGHTS_0")->second;
            }
            int idxIdx = prim.indices;

            const tinygltf::Accessor& pos = model.accessors[posIdx];
            const tinygltf::Accessor& norm = model.accessors[normIdx];
            const tinygltf::Accessor& uv = model.accessors[uvIdx];
            const tinygltf::Accessor& indices = model.accessors[idxIdx];

            const tinygltf::Accessor* pTg = tgIdx == -1 ? nullptr : &model.accessors[tgIdx];
            const tinygltf::Accessor* pJoints = jointsIdx == -1 ? nullptr : &model.accessors[jointsIdx];
            const tinygltf::Accessor* pWeights = weightsIdx == -1 ? nullptr : &model.accessors[weightsIdx];

            assert(pos.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(pos.type == TINYGLTF_TYPE_VEC3);
            assert(indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT || indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
            assert(indices.type == TINYGLTF_TYPE_SCALAR);

            assert(pJoints == nullptr || pJoints->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
            assert(pJoints == nullptr || pJoints->type == TINYGLTF_TYPE_VEC4);
            assert(pWeights == nullptr || pWeights->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(pWeights == nullptr || pWeights->type == TINYGLTF_TYPE_VEC4);
            if (pTg != nullptr)
            {
                assert(pTg == nullptr || pTg->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
                assert(pTg == nullptr || pTg->type == TINYGLTF_TYPE_VEC4);
            }

            const tinygltf::BufferView& posView = model.bufferViews[pos.bufferView];
            const tinygltf::BufferView& normView = model.bufferViews[norm.bufferView];
            const tinygltf::BufferView& uvView = model.bufferViews[uv.bufferView];
            const tinygltf::BufferView& indicesView = model.bufferViews[indices.bufferView];

            const tinygltf::BufferView* pTgView = tgIdx == -1 ? nullptr : &model.bufferViews[pTg->bufferView];
            const tinygltf::BufferView* pJointsView = jointsIdx == -1 ? nullptr : &model.bufferViews[pJoints->bufferView];
            const tinygltf::BufferView* pWeightsView = weightsIdx == -1 ? nullptr : &model.bufferViews[pWeights->bufferView];

            const void* pIndices = reinterpret_cast<const void*>(model.buffers[indicesView.buffer].data.data() + indicesView.byteOffset + indices.byteOffset);
            const Point3f* pPos = reinterpret_cast<const Point3f*>(model.buffers[posView.buffer].data.data() + posView.byteOffset + pos.byteOffset);
            const Point3f* pNorm = reinterpret_cast<const Point3f*>(model.buffers[normView.buffer].data.data() + normView.byteOffset + norm.byteOffset);
            const Point2f* pUV = reinterpret_cast<const Point2f*>(model.buffers[uvView.buffer].data.data() + uvView.byteOffset + uv.byteOffset);

            const Point4f* pTang = tgIdx == -1 ? nullptr : reinterpret_cast<const Point4f*>(model.buffers[pTgView->buffer].data.data() + pTgView->byteOffset + pTg->byteOffset);
            const Point4<unsigned short>* pJointsValues = jointsIdx == -1 ? nullptr : reinterpret_cast<const Point4<unsigned short>*>(model.buffers[pJointsView->buffer].data.data() + pJointsView->byteOffset + pJoints->byteOffset);
            const Point4f* pWeightsValues = weightsIdx == -1 ? nullptr : reinterpret_cast<const Point4f*>(model.buffers[pWeightsView->buffer].data.data() + pWeightsView->byteOffset + pWeights->byteOffset);

            std::vector<NormalVertex> vertices;
            std::vector<NormalWeightedVertex> weightVertices;

            if (pJointsValues == nullptr)
            {
                vertices.resize(pos.count);
                for (int i = 0; i < pos.count; i++)
                {
                    vertices[i].pos = pPos[i];
                    vertices[i].normal = pNorm[i];
                    if (pTang != nullptr)
                    {
                        vertices[i].tangent = pTang[i];
                    }
                    vertices[i].uv = pUV[i];
                }
            }
            else
            {
                weightVertices.resize(pos.count);
                for (int i = 0; i < pos.count; i++)
                {
                    weightVertices[i].pos = pPos[i];
                    weightVertices[i].normal = pNorm[i];
                    if (pTang != nullptr)
                    {
                        weightVertices[i].tangent = pTang[i];
                    }
                    weightVertices[i].uv = pUV[i];

                    weightVertices[i].joints = pJointsValues[i];
                    weightVertices[i].weights = pWeightsValues[i];
                }
            }

            GLTFGeometry* pGeometry = new GLTFGeometry();
            BaseRenderer::CreateGeometryParams params;

            params.geomAttributes.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });
            params.geomAttributes.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12 });
            params.geomAttributes.push_back({ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 24 });
            params.geomAttributes.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 40 });
            if (pJointsValues != nullptr)
            {
                params.geomAttributes.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_UINT, 48 });
                params.geomAttributes.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 56 });

                params.shaderDefines.push_back("SKINNED");
            }

            if (indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                params.indexDataSize = (UINT)(indices.count * sizeof(UINT32));
            }
            else
            {
                params.indexDataSize = (UINT)(indices.count * sizeof(UINT16));
            }
            params.indexFormat = indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
            params.pIndices = pIndices;

            params.primTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            params.pShaderSourceName = _T("Material.hlsl");

            if (pJointsValues == nullptr)
            {
                params.pVertices = vertices.data();
                params.vertexDataSize = (UINT)(vertices.size() * sizeof(NormalVertex));
                params.vertexDataStride = sizeof(NormalVertex);
            }
            else
            {
                params.pVertices = weightVertices.data();
                params.vertexDataSize = (UINT)(weightVertices.size() * sizeof(NormalWeightedVertex));
                params.vertexDataStride = sizeof(NormalWeightedVertex);
            }

            params.rasterizerState.FrontCounterClockwise = FALSE;
            params.rtFormat = m_hdrFormat;
            params.rtFormat2 = m_hdrFormat;

            params.geomStaticTexturesCount = 0;

            Point4f emissiveFactor = {};
            bool blend = false;
            bool alphaKill = false;

            bool isKHRSpecGloss = false;
            bool plainColor = false;
            bool plainFeature = true;
            bool normalMap = false;
            bool emissive = false;
            bool emissiveMap = false;

            if (prim.material != -1)
            {
                const tinygltf::Material& material = model.materials[prim.material];

                // Parse KHR_materials_pbrSpecularGlossiness extension
                const tinygltf::Value* pKHRSpecGloss = nullptr;
                isKHRSpecGloss = material.extensions.find("KHR_materials_pbrSpecularGlossiness") != material.extensions.end();
                if (isKHRSpecGloss)
                {
                    pKHRSpecGloss = &material.extensions.at("KHR_materials_pbrSpecularGlossiness");
                    params.shaderDefines.push_back("KHR_SPECGLOSS");
                }

                if (material.alphaMode == "BLEND")
                {
                    blend = true;
                    params.shaderDefines.push_back("TRANSPARENT");
                }
                else if (material.alphaMode == "MASK")
                {
                    alphaKill = true;
                }
                params.geomStaticTexturesCount = 4;

                int diffuseIdx = -1;
                if (!isKHRSpecGloss)
                {
                    diffuseIdx = material.pbrMetallicRoughness.baseColorTexture.index;
                }
                else
                {
                    const tinygltf::Value& diff = pKHRSpecGloss->Get("diffuseTexture");
                    if (diff.IsObject())
                    {
                        diffuseIdx = diff.Get("index").GetNumberAsInt();
                    }
                }

                if (diffuseIdx != -1)
                {
                    const tinygltf::Texture& texture = model.textures[diffuseIdx];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);
                }
                else
                {
                    params.shaderDefines.push_back("PLAIN_COLOR");
                    plainColor = true;

                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }

                int featureIdx = -1;
                if (!isKHRSpecGloss)
                {
                    featureIdx = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
                }
                else
                {
                    const tinygltf::Value& specGlos = pKHRSpecGloss->Get("specularGlossinessTexture");
                    if (specGlos.IsObject())
                    {
                        featureIdx = specGlos.Get("index").GetNumberAsInt();
                    }
                }

                if (featureIdx != -1)
                {
                    const tinygltf::Texture& texture = model.textures[featureIdx];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);

                    plainFeature = false;
                }
                else
                {
                    if (isKHRSpecGloss)
                    {
                        params.shaderDefines.push_back("PLAIN_SPEC_GLOSS");
                    }
                    else
                    {
                        params.shaderDefines.push_back("PLAIN_METAL_ROUGH");
                    }

                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }

                if (material.normalTexture.index != -1 && tgIdx != -1)
                {
                    params.shaderDefines.push_back("NORMAL_MAP");
                    const tinygltf::Texture& texture = model.textures[material.normalTexture.index];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);

                    normalMap = true;
                }
                else
                {
                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }

                if (material.emissiveTexture.index != -1)
                {
                    emissiveMap = true;
                    params.shaderDefines.push_back("EMISSIVE_MAP");
                    const tinygltf::Texture& texture = model.textures[material.emissiveTexture.index];
                    Platform::GPUResource resource = textures[texture.source];

                    params.geomStaticTextures.push_back(resource.pResource);
                }
                else
                {
                    // Dummy texture
                    Platform::GPUResource resource = textures[0];
                    params.geomStaticTextures.push_back(resource.pResource);
                }

                // We may assume every material is double sided, 
                // It doesn't play a role for opaque, and correct for transparent ones
                {
                    params.rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                }
                emissiveFactor = Point4f{
                    (float)material.emissiveFactor[0],
                    (float)material.emissiveFactor[1],
                    (float)material.emissiveFactor[2],
                    0.0f
                };
                if (emissiveFactor.lengthSqr() > 0.01f)
                {
                    emissive = true;
                    params.shaderDefines.push_back("EMISSIVE");
                }
            }

            if (blend)
            {
                params.blendState.RenderTarget[0].BlendEnable = TRUE;
                params.blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                params.blendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                params.blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                params.blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                params.blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
                params.blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;

                params.depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            }

            if (alphaKill)
            {
                params.shaderDefines.push_back("ALPHA_KILL");
            }

            if (m_useLocalCubemaps)
            {
                params.shaderDefines.push_back("USE_LOCAL_CUBEMAPS");
            }

            res = m_pRenderer->CreateGeometry(params, *pGeometry);
            if (res && !blend) // AAV TEMP
            {
                BaseRenderer::GeometryStateParams cubeParams = params;
                cubeParams.rtFormat = m_cubeHDRFormat;
                cubeParams.rtFormat2 = DXGI_FORMAT_UNKNOWN;
                cubeParams.shaderDefines.push_back("NO_BLOOM");
                cubeParams.shaderDefines.push_back("NO_POINT_LIGHTS");

                BaseRenderer::GeometryState* pCubeState = new BaseRenderer::GeometryState();
                res = m_pRenderer->CreateGeometryState(cubeParams, *pCubeState);
                if (res)
                {
                    m_modelLoadState.pGLTFModel->cubePassStates.push_back(pCubeState);
                }
            }
            if (res && !blend)
            {
                BaseRenderer::GeometryStateParams zParams = params;
                zParams.pShaderSourceName = _T("ZPass.hlsl");
                zParams.shaderDefines.clear();
                if (isKHRSpecGloss)
                {
                    zParams.shaderDefines.push_back("KHR_SPECGLOSS");
                }
                if (plainColor)
                {
                    zParams.shaderDefines.push_back("PLAIN_COLOR");
                }
                if (alphaKill)
                {
                    zParams.shaderDefines.push_back("ALPHA_KILL");
                }
                if (pJointsValues != nullptr)
                {
                    zParams.shaderDefines.push_back("SKINNED");
                }
                if (m_zPassNormals)
                {
                    if (normalMap)
                    {
                        zParams.shaderDefines.push_back("NORMAL_MAP");
                    }
                    zParams.geomStaticTexturesCount = 3;
                }
                else
                {
                    zParams.geomStaticTexturesCount = 1;
                }
                zParams.blendState.RenderTarget[0].BlendEnable = FALSE;
                zParams.rtFormat = m_zPassNormals ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_UNKNOWN;
                zParams.rtFormat2 = DXGI_FORMAT_UNKNOWN;

                ZPassState state;

                // Real Z-pass states
                BaseRenderer::GeometryState* pZState = new BaseRenderer::GeometryState();
                res = m_pRenderer->CreateGeometryState(zParams, *pZState);
                if (res)
                {
                    state.states[ZPassTypeSimple] = pZState;
                }
                if (res)
                {
                    pZState = new BaseRenderer::GeometryState();
                    zParams.rasterizerState.DepthBias = 32;
                    res = m_pRenderer->CreateGeometryState(zParams, *pZState);
                    state.states[ZPassTypeBias] = pZState;
                }
                if (res)
                {
                    pZState = new BaseRenderer::GeometryState();
                    zParams.rasterizerState.SlopeScaledDepthBias = sqrtf(2.0f) * 2.0f;
                    res = m_pRenderer->CreateGeometryState(zParams, *pZState);
                    state.states[ZPassTypeBiasSlopeScale] = pZState;
                }

                // G-buffer pass for deferred render
                if (m_forDeferred)
                {
                    if (res)
                    {
                        BaseRenderer::GeometryStateParams zParams = params;
                        zParams.pShaderSourceName = _T("GBuffer.hlsl");
                        zParams.shaderDefines.clear();
                        if (isKHRSpecGloss)
                        {
                            zParams.shaderDefines.push_back("KHR_SPECGLOSS");
                        }
                        if (plainColor)
                        {
                            zParams.shaderDefines.push_back("PLAIN_COLOR");
                        }
                        if (plainFeature)
                        {
                            if (isKHRSpecGloss)
                            {
                                zParams.shaderDefines.push_back("PLAIN_SPEC_GLOSS");
                            }
                            else
                            {
                                zParams.shaderDefines.push_back("PLAIN_METAL_ROUGH");
                            }
                        }
                        if (alphaKill)
                        {
                            zParams.shaderDefines.push_back("ALPHA_KILL");
                        }
                        if (normalMap)
                        {
                            zParams.shaderDefines.push_back("NORMAL_MAP");
                        }
                        if (emissive)
                        {
                            zParams.shaderDefines.push_back("EMISSIVE");
                            if (emissiveMap)
                            {
                                zParams.shaderDefines.push_back("EMISSIVE_MAP");
                            }
                        }
                        if (pJointsValues != nullptr)
                        {
                            zParams.shaderDefines.push_back("SKINNED");
                        }
                        zParams.geomStaticTexturesCount = 4;
                        zParams.blendState.RenderTarget[0].BlendEnable = FALSE;
                        zParams.rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                        zParams.rtFormat2 = DXGI_FORMAT_R8G8B8A8_UNORM;
                        zParams.rtFormat3 = DXGI_FORMAT_R8G8B8A8_UNORM;
                        if (emissive)
                        {
                            zParams.rtFormat4 = DXGI_FORMAT_R8G8B8A8_UNORM;
                        }

                        pZState = new BaseRenderer::GeometryState();
                        res = m_pRenderer->CreateGeometryState(zParams, *pZState);
                        state.states[ZPassGBuffer] = pZState;
                    }
                }

                if (res)
                {
                    m_modelLoadState.pGLTFModel->zPassGeomStates.push_back(state);
                }
            }

            if (res)
            {
                if (prim.material != -1)
                {
                    const tinygltf::Material& material = model.materials[prim.material];
                    pGeometry->splitData.metalF0 = Point4f{
                        (float)material.pbrMetallicRoughness.baseColorFactor[0],
                        (float)material.pbrMetallicRoughness.baseColorFactor[1],
                        (float)material.pbrMetallicRoughness.baseColorFactor[2],
                        (float)material.pbrMetallicRoughness.baseColorFactor[3]
                    };

                    pGeometry->splitData.pbr.x = (float)material.pbrMetallicRoughness.roughnessFactor;
                    pGeometry->splitData.pbr.y = (float)material.pbrMetallicRoughness.metallicFactor;
                    pGeometry->splitData.pbr.w = (float)pow(material.alphaCutoff, 2.2); // Take srgb texture reading into account

                    bool isKHRSpecGloss = material.extensions.find("KHR_materials_pbrSpecularGlossiness") != material.extensions.end();
                    if (isKHRSpecGloss)
                    {
                        const tinygltf::Value& ksg = material.extensions.at("KHR_materials_pbrSpecularGlossiness");
                        if (ksg.Has("diffuseFactor"))
                        {
                            const tinygltf::Value& diffFactor = ksg.Get("diffuseFactor");
                            pGeometry->splitData.ksgDiffFactor = Point4f{
                                (float)diffFactor.Get(0).GetNumberAsDouble(),
                                (float)diffFactor.Get(1).GetNumberAsDouble(),
                                (float)diffFactor.Get(2).GetNumberAsDouble(),
                                (float)diffFactor.Get(3).GetNumberAsDouble(),
                            };
                        }
                        Point4f specGlosFactor = Point4f{ 1,1,1,1 };
                        if (ksg.Has("specularFactor"))
                        {
                            const tinygltf::Value& specFactor = ksg.Get("specularFactor");
                            specGlosFactor.x = (float)specFactor.Get(0).GetNumberAsDouble();
                            specGlosFactor.y = (float)specFactor.Get(1).GetNumberAsDouble();
                            specGlosFactor.z = (float)specFactor.Get(2).GetNumberAsDouble();
                        }
                        if (ksg.Has("glossinessFactor"))
                        {
                            const tinygltf::Value& glosFactor = ksg.Get("glossinessFactor");
                            specGlosFactor.w = (float)glosFactor.GetNumberAsDouble();
                        }
                        pGeometry->splitData.ksgSpecGlossFactor = specGlosFactor;
                    }
                }

                pGeometry->splitData.nodeIndex = Point4i(nodeIdx);

                pGeometry->splitData.pbr.z = 0.04f;

                pGeometry->splitData.emissiveFactor = emissiveFactor;

                if (blend)
                {
                    m_modelLoadState.pGLTFModel->blendGeometries.push_back(pGeometry);
                }
                else
                {
                    m_modelLoadState.pGLTFModel->geometries.push_back(pGeometry);
                }
            }
        }
    }

    for (int i = 0; i < node.children.size() && res; i++)
    {
        m_modelLoadState.pGLTFModel->nodes[nodeIdx].children.push_back(node.children[i]);

        res = ScanNode(model, node.children[i], textures);
    }

    return res;
}

AABB<float> ModelLoader::CalcModelAABB(const tinygltf::Model& model, int nodeIdx, const Matrix4f* pTransforms)
{
    const tinygltf::Node& node = model.nodes[nodeIdx];
    Matrix4f m = pTransforms[nodeIdx];

    AABB<float> res;

    if (node.mesh != -1)
    {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];

        for (int i = 0; i < mesh.primitives.size(); i++)
        {
            const tinygltf::Primitive& prim = mesh.primitives[i];
            assert(prim.mode == TINYGLTF_MODE_TRIANGLES);

            int posIdx = prim.attributes.find("POSITION")->second;
            const tinygltf::Accessor& pos = model.accessors[posIdx];
            const tinygltf::BufferView& posView = model.bufferViews[pos.bufferView];
            const Point3f* pPos = reinterpret_cast<const Point3f*>(model.buffers[posView.buffer].data.data() + posView.byteOffset + pos.byteOffset);

            for (int i = 0; i < pos.count; i++)
            {
                Point4f mp = m * Point4f{ pPos[i].x, pPos[i].y, pPos[i].z, 1.0f };

                Point3f p = Point3f{ mp.x, mp.y, mp.z };

                res.Add(p);
            }
        }
    }

    for (int i = 0; i < node.children.size(); i++)
    {
        auto childRes = CalcModelAABB(model, node.children[i], pTransforms);
        if (childRes.IsEmpty())
        {
            continue;
        }
        res.Add(childRes.bbMin);
        res.Add(childRes.bbMax);
    }

    return res;
}

void ModelLoader::SetupModelScale()
{
    auto bb = CalcModelAABB(*m_modelLoadState.pModel, m_modelLoadState.pGLTFModel->rootNodeIdx, m_modelLoadState.pGLTFModel->objData.nodeTransforms);
    Point3f size = bb.GetSize();

    float scaleValue = 1.0f;
    if (m_modelLoadState.autoscale)
    {
        // Calculate max size
        float maxSize = std::max(std::max(size.x, size.y), size.z);

        // Some heuristics to calculate realistic scale for object
        float sizeNorm = pow(10.0f, floor(log10f(maxSize)));

        scaleValue = 1.0f / sizeNorm;
    }
    else
    {
        scaleValue = m_modelLoadState.scaleValue;
    }

    m_modelLoadState.pGLTFModel->scaleValue = scaleValue;
    m_modelLoadState.pGLTFModel->offset = -bb.bbMin - Point3f{ size.x / 2, 0, size.z / 2 };

    Matrix4f scale;
    scale.Scale(scaleValue, scaleValue, scaleValue);

    Matrix4f trans;
    Point3f offset = -bb.bbMin - Point3f{ size.x / 2, 0, size.z / 2 };
    trans.Offset(offset);

    m_modelLoadState.pGLTFModel->bbMin = trans * scale * Point4f{ bb.bbMin, 1.0f };
    m_modelLoadState.pGLTFModel->bbMax = trans * scale * Point4f{ bb.bbMax, 1.0f };

    m_modelLoadState.pGLTFModel->UpdateMatrices();
}

void ModelLoader::LoadSkins()
{
    if (!m_modelLoadState.pModel->skins.empty())
    {
        m_modelLoadState.pGLTFModel->skinned = true;

        assert(m_modelLoadState.pModel->skins.size() == 1);

        tinygltf::Accessor invBindMatricesAccessor = m_modelLoadState.pModel->accessors[m_modelLoadState.pModel->skins[0].inverseBindMatrices];

        assert(invBindMatricesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
        assert(invBindMatricesAccessor.type == TINYGLTF_TYPE_MAT4);

        tinygltf::BufferView invBindMatricesView = m_modelLoadState.pModel->bufferViews[invBindMatricesAccessor.bufferView];
        tinygltf::Buffer invBindMatricesBuffer = m_modelLoadState.pModel->buffers[invBindMatricesView.buffer];

        const Matrix4f* pInvBindMatrices = reinterpret_cast<const Matrix4f*>(invBindMatricesBuffer.data.data() + invBindMatricesView.byteOffset + invBindMatricesAccessor.byteOffset);

        assert(m_modelLoadState.pModel->skins[0].joints.size() <= MAX_NODES);

        for (int i = 0; i < m_modelLoadState.pModel->skins[0].joints.size(); i++)
        {
            ((int*)m_modelLoadState.pGLTFModel->objData.jointIndices)[i] = m_modelLoadState.pModel->skins[0].joints[i];

            int nodeIdx = m_modelLoadState.pModel->skins[0].joints[i];

            if (nodeIdx >= m_modelLoadState.pGLTFModel->nodeInvBindMatrices.size())
            {
                m_modelLoadState.pGLTFModel->nodeInvBindMatrices.resize(nodeIdx + 1, Matrix4f());
            }
            m_modelLoadState.pGLTFModel->nodeInvBindMatrices[nodeIdx] = pInvBindMatrices[i];
        }
    }
}

void ModelLoader::LoadAnimations()
{
    if (!m_modelLoadState.pModel->animations.empty())
    {
        m_modelLoadState.pGLTFModel->maxAnimationTime = 0.0f;
        m_modelLoadState.pGLTFModel->minAnimationTime = std::numeric_limits<float>::max();

        assert(m_modelLoadState.pModel->animations.size() == 1);

        for (int i = 0; i < m_modelLoadState.pModel->animations[0].samplers.size(); i++)
        {
            GLTFModel::AnimationSampler animSampler;

            tinygltf::Accessor timeKeysAccessor = m_modelLoadState.pModel->accessors[m_modelLoadState.pModel->animations[0].samplers[i].input];

            assert(timeKeysAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(timeKeysAccessor.type == TINYGLTF_TYPE_SCALAR);

            tinygltf::BufferView timeKeysView = m_modelLoadState.pModel->bufferViews[timeKeysAccessor.bufferView];
            tinygltf::Buffer timeKeysBuffer = m_modelLoadState.pModel->buffers[timeKeysView.buffer];

            const float* timeKeys = reinterpret_cast<const float*>(timeKeysBuffer.data.data() + timeKeysView.byteOffset + timeKeysAccessor.byteOffset);

            animSampler.timeKeys.resize(timeKeysAccessor.count);
            for (int j = 0; j < timeKeysAccessor.count; j++)
            {
                animSampler.timeKeys[j] = timeKeys[j];
            }

            m_modelLoadState.pGLTFModel->maxAnimationTime = std::max(m_modelLoadState.pGLTFModel->maxAnimationTime, animSampler.timeKeys.back());
            m_modelLoadState.pGLTFModel->minAnimationTime = std::min(m_modelLoadState.pGLTFModel->minAnimationTime, animSampler.timeKeys.front());

            tinygltf::Accessor keysAccessor = m_modelLoadState.pModel->accessors[m_modelLoadState.pModel->animations[0].samplers[i].output];

            assert(keysAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            assert(keysAccessor.type == TINYGLTF_TYPE_VEC3 || keysAccessor.type == TINYGLTF_TYPE_VEC4);

            tinygltf::BufferView keysView = m_modelLoadState.pModel->bufferViews[keysAccessor.bufferView];
            tinygltf::Buffer keysBuffer = m_modelLoadState.pModel->buffers[keysView.buffer];

            const Point3f* keys3f = nullptr;
            const Point4f* keys4f = nullptr;

            if (keysAccessor.type == TINYGLTF_TYPE_VEC3)
            {
                keys3f = reinterpret_cast<const Point3f*>(keysBuffer.data.data() + keysView.byteOffset + keysAccessor.byteOffset);
            }
            else if (keysAccessor.type == TINYGLTF_TYPE_VEC4)
            {
                keys4f = reinterpret_cast<const Point4f*>(keysBuffer.data.data() + keysView.byteOffset + keysAccessor.byteOffset);
            }

            assert(timeKeysAccessor.count == keysAccessor.count);
            animSampler.keys.resize(keysAccessor.count);
            for (int j = 0; j < timeKeysAccessor.count; j++)
            {
                if (keys3f)
                {
                    animSampler.keys[j] = Point4f(keys3f[j].x, keys3f[j].y, keys3f[j].z, 0.0f);
                }
                else if (keys4f)
                {
                    animSampler.keys[j] = keys4f[j];
                }
            }

            m_modelLoadState.pGLTFModel->animationSamplers.push_back(animSampler);
        }

        for (auto& animSampler : m_modelLoadState.pGLTFModel->animationSamplers)
        {
            for (auto& timeKey : animSampler.timeKeys)
            {
                timeKey -= m_modelLoadState.pGLTFModel->minAnimationTime;
            }
        }
        m_modelLoadState.pGLTFModel->maxAnimationTime -= m_modelLoadState.pGLTFModel->minAnimationTime;
        m_modelLoadState.pGLTFModel->minAnimationTime = 0.0f;

        for (int i = 0; i < m_modelLoadState.pModel->animations[0].channels.size(); i++)
        {
            GLTFModel::AnimationChannel animChannel;

            if (m_modelLoadState.pModel->animations[0].channels[i].target_path == "rotation")
            {
                animChannel.type = GLTFModel::AnimationChannel::Rotation;
            }
            else if (m_modelLoadState.pModel->animations[0].channels[i].target_path == "translation")
            {
                animChannel.type = GLTFModel::AnimationChannel::Translation;
            }
            else if (m_modelLoadState.pModel->animations[0].channels[i].target_path == "scale")
            {
                animChannel.type = GLTFModel::AnimationChannel::Scale;
            }
            animChannel.animSamplerIdx = m_modelLoadState.pModel->animations[0].channels[i].sampler;
            animChannel.nodeIdx = m_modelLoadState.pModel->animations[0].channels[i].target_node;

            m_modelLoadState.pGLTFModel->animationChannels.push_back(animChannel);
        }
    }
}

} // Platform
