#pragma once
#include "Camera.h"
#include "CameraController.h"
#include "Math/VectorMath.h"
#include "GraphicsContext.h"
#include "ConstantBuffer.h"
#include "GpuBuffer.h"
#include "Texture.h"

class Model;
class CameraController;
class GraphicsCommandList;

class Scene : public MutiGraphicsContext
{
public:
	Scene() {}
	~Scene() {}

    void Startup();

    GraphicsCommandList* RenderScene(GraphicsCommandList* context);
    GraphicsCommandList* RenderSkyBox(GraphicsCommandList* context);

    virtual void Update(float deltaTime) override;

    virtual void Render() override;

    void SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL);

    void SetIBLBias(float LODBias);

    void UpdateGlobalDescriptors();

    Math::BoundingSphere UpdateModelBoundingSphere();
public:
	Math::Camera mSceneCamera;
	ShadowCamera mShadowCamera;
    std::unique_ptr<CameraController> m_CameraController;
    float mSunDirectionTheta;
    float mSunDirectionPhi;
    Vector3 mSunLightIntensity;

	std::vector<Model> mModels;
	std::vector<Math::UniformTransform> mModelTransform;
    GlobalConstants mGlobalConstants;
    UploadBuffer mMeshConstantsUploader;
    Math::BoundingSphere mSceneBoundingSphere;

    TextureRef mRadianceCubeMap;
    TextureRef mIrradianceCubeMap;
    float mSpecularIBLRange;
    float mSpecularIBLBias;
};
