#include "GameApp.h"
#include "Scene.h"
#include "ModelConverter.h"
#include "glTF.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "Texture.h"

static glTF::Asset sAsset;
static Scene* sScenePtr;

namespace GameApp
{
	class SceneGameApp : public IGameApp
	{
		virtual void Start() override;

		virtual void RegisterContext() override;

		virtual void Update(float deltaTime);

		virtual void Cleanup();
	};


	void SceneGameApp::Start()
	{
		sAsset.Parse(L"Asset/Sponza2/sponza2.gltf");

		ModelConverter::BuildMaterials(sAsset);
		ModelConverter::BuildAllMeshes(sAsset);
		ModelConverter::BuildScene(sScenePtr, sAsset);

		sScenePtr->Startup();

		TextureRef radianceIBL = GET_TEX(ModelConverter::GetIBLTextureFilename(L"CloudCommon_S"));
		TextureRef irradianceIBL = GET_TEX(ModelConverter::GetIBLTextureFilename(L"CloudCommon_D"));
		sScenePtr->SetIBLTextures(irradianceIBL, radianceIBL);
	}

	void SceneGameApp::RegisterContext()
	{
		MaterialManager::GetOrCreateInstance();
		MeshManager::GetOrCreateInstance();

		sScenePtr = REGISTER_CONTEXT(Scene);

		ModelRenderer::Initialize();
	}

	void SceneGameApp::Update(float deltaTime)
	{
		MaterialManager::GetInstance()->Update();

		sScenePtr->Update(deltaTime);
	}

	void SceneGameApp::Cleanup()
	{
		ModelRenderer::Destroy();

		MaterialManager::RemoveInstance();
		MeshManager::RemoveInstance();
	}
}

CREATE_APPLICATION(GameApp::SceneGameApp);
