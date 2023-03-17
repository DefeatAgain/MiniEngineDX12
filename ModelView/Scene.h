#pragma once
#include "Camera.h"
#include "CameraController.h"
#include "Math/VectorMath.h"
#include "GraphicsContext.h"
#include "ConstantBuffer.h"
#include "GpuBuffer.h"
#include "Texture.h"
#include "Model.h"

class CameraController;
class GraphicsCommandList;
class MeshRendererBuilder;

namespace glTF
{
    struct Node;
}

namespace MainView
{
    void ShowUI(Scene*);
}

enum SceneTextureHandles
{
    kRadianceIBLTexture,
    kIrradianceIBLTexture,
    kPreComputeGGXBRDFTexture,
    kSunShadowTexture,
    kNumRootBindings
};

class Scene : public Graphics::MutiGraphicsContext
{
    friend void MainView::ShowUI(Scene*);
public:
	Scene() {}
    ~Scene() { Destroy(); }

    void Destroy();

    virtual void Initialize() override {}

    void Startup();

    void Update(float deltaTime);

    virtual void Render() override;

    void SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL);
    void SetIBLRange(float range) { mSpecularIBLRange = range; }

    void ResizeModels(size_t numModels) { mModels.resize(numModels);  }
    void WalkGraph(const std::vector<glTF::Node*>& siblings, uint32_t curIndex, const Math::Matrix4& xform);

    const Model& GetModel(size_t index) const { return mModels[index]; }
    const Math::AffineTransform& GetModelTranform(size_t index) const { return mModelWorldTransform[index]; }

    float GetIBLRange() const { return mSpecularIBLRange; }

    DescriptorHandle GetSceneTextureHandles() const { return mSceneTextureGpuHandle; }
    DescriptorHandle GetShadowTextureHandle() const;

    void ResetShadowMap();
private:
    void SetRenderModels(MeshRenderer& renderer);
    std::shared_ptr<MeshRendererBuilder> SetMeshRenderers();

    void UpdateModels();
    void UpdateLight();

    void MapGpuDescriptors();
    
    void UpdateModelBoundingSphere();

    CommandList* RenderScene(CommandList* context, std::shared_ptr<MeshRendererBuilder> meshRendererBuilder);
    void RenderSkyBox(GraphicsCommandList& context);
private:
	Math::Camera mSceneCamera;
    std::vector<ShadowCamera>  mShadowCameras;
    std::unique_ptr<CameraController> mCameraController;
    float mSunDirectionTheta;
    float mSunDirectionPhi;
    Vector3 mSunLightIntensity;
    Vector3 mSunDirection;

	std::vector<Model> mModels;
	std::vector<Math::AffineTransform> mModelWorldTransform;
    GlobalConstants mGlobalConstants;
    UploadBuffer mMeshConstantsUploader[SWAP_CHAIN_BUFFER_COUNT];
    Math::BoundingSphere mSceneBS_WS;
    size_t mModelDirtyFrameCount;

    TextureRef mRadianceCubeMap;
    TextureRef mIrradianceCubeMap;
    float mSpecularIBLRange;
    float mShadowBias;

    DescriptorHandle mShadowGpuHandle;
    DescriptorHandle mSceneTextureGpuHandle;
};
