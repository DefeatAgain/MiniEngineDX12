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

class Scene : public MutiGraphicsContext
{
public:
	Scene() {}
	~Scene() {}

    void Startup();

    void Cleanup();

    void RenderScene(const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);

    virtual void Update(float deltaTime) override;

    virtual void Render() override;

    void SetIBLTextures(TextureRef diffuseIBL, TextureRef specularIBL);

    void SetIBLBias(float LODBias);

    void UpdateGlobalDescriptors();

	Math::Camera mSceneCamera;
    std::unique_ptr<CameraController> m_CameraController;
	std::vector<Model> mModels;
	std::vector<MeshConstants> mMeshConstants;
    GlobalConstants globalConstants;
private:
    UploadBuffer mMeshConstants;
    TextureRef mRadianceCubeMap;
    TextureRef mIrradianceCubeMap;
    float mSpecularIBLRange;
    float mSpecularIBLBias;
    uint32_t mSSAOFullScreenID;
    uint32_t mShadowBufferID;
};
