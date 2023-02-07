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

class Scene : public Graphics::MutiGraphicsContext
{
public:
	Scene() {}
	~Scene() {}

    virtual void Initialize() override {}

    void Startup();

    virtual void Update(float deltaTime) override;

    virtual void Render() override;

    void SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL)
    {
        mRadianceCubeMap = specularIBL;
        mIrradianceCubeMap = diffuseIBL;
    }

    void SetIBLBias(float LODBias) { mSpecularIBLBias = LODBias; }

    void SetIBLRange(float range) { mSpecularIBLRange = range; }

    void SetModels(std::vector<Model>&& modelMoved) { mModels = std::move(modelMoved); }

    std::vector<Model>& GetModels() { return mModels; }
    std::vector<Math::AffineTransform>& GetModelTranforms() { return mModelTransform; }

    float GetIBLBias() const { return mSpecularIBLBias; }
    float GetIBLRange() const { return mSpecularIBLRange; }

    void UpdateModels();
private:
    void UpdateModelBoundingSphere();

    CommandList* RenderScene(CommandList* context);
    void RenderSkyBox(GraphicsCommandList& context);
private:
	Math::Camera mSceneCamera;
	ShadowCamera mShadowCamera;
    std::unique_ptr<CameraController> mCameraController;
    float mSunDirectionTheta;
    float mSunDirectionPhi;
    Vector3 mSunLightIntensity;

	std::vector<Model> mModels;
	std::vector<Math::AffineTransform> mModelTransform;
    GlobalConstants mGlobalConstants;
    UploadBuffer mMeshConstantsUploader;
    Math::BoundingSphere mSceneBoundingSphere;
    bool mDirtyModels;

    TextureRef mRadianceCubeMap;
    TextureRef mIrradianceCubeMap;
    float mSpecularIBLRange;
    float mSpecularIBLBias;
};
